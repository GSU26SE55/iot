pipeline {
    agent none

    options {
        disableConcurrentBuilds()
        timestamps()
        timeout(time: 90, unit: 'MINUTES')
        buildDiscarder(
            logRotator(
                numToKeepStr: '30',
                artifactNumToKeepStr: '15'
            )
        )
        skipDefaultCheckout(true)
    }

    stages {
        stage('CI and security gates') {
            agent {
                label 'docker-linux'
            }

            stages {
                stage('Checkout') {
                    steps {
                        checkout scm
                        script {
                            env.GIT_SHA = sh(
                                script: 'git rev-parse HEAD',
                                returnStdout: true
                            ).trim()
                            if (!(env.GIT_SHA ==~ /[0-9a-f]{40}/)) {
                                error('Unable to resolve a full immutable Git SHA')
                            }
                        }
                        sh 'git diff --check'
                    }
                }

                stage('Executor preflight') {
                    steps {
                        sh '''
                            set -eu

                            for tool in \
                              git docker python3.11 pio shellcheck trivy syft openssl
                            do
                              command -v "${tool}" >/dev/null 2>&1 || {
                                echo "Missing required Jenkins tool: ${tool}" >&2
                                exit 1
                              }
                            done
                            docker info >/dev/null
                            docker compose version >/dev/null
                        '''
                    }
                }

                stage('Simulator contracts') {
                    steps {
                        sh '''
                            set -eu
                            python3.11 -m unittest discover \
                              -s tools/simulator \
                              -p 'test_*.py' \
                              -v
                        '''
                    }
                }

                stage('Firmware native tests') {
                    steps {
                        sh '''
                            set -eu
                            cd firmware-esp32
                            pio test -e native --verbose
                        '''
                    }
                }

                stage('Firmware production builds') {
                    steps {
                        sh '''
                            set -eu
                            cd firmware-esp32
                            cp include/config.example.h include/config.h

                            grep -Eq \
                              '^#define[[:space:]]+BACKEND_URL[[:space:]]+"https://api[.]solars[.]io[.]vn"$' \
                              include/config.h
                            grep -Eq \
                              '^#define[[:space:]]+MQTT_BROKER_HOST[[:space:]]+"mqtt[.]solars[.]io[.]vn"([[:space:]]|$)' \
                              include/config.h
                            grep -Eq \
                              '^#define[[:space:]]+TLS_ALLOW_INSECURE[[:space:]]+0$' \
                              include/config.h

                            pio run -e esp32-s3-devkitc-1
                            pio run -e esp32-s3-real
                            pio run -e esp32-s3-uartlog
                            pio run -e esp32-s3-real-uartlog
                            pio run -e example-blink

                            firmware_size="$(stat -c%s .pio/build/esp32-s3-devkitc-1/firmware.bin)"
                            test "${firmware_size}" -le 2097152 || {
                              echo "Firmware is larger than 2 MiB: ${firmware_size}" >&2
                              exit 1
                            }

                            mkdir -p ../firmware-artifacts
                            cp .pio/build/esp32-s3-devkitc-1/firmware.bin \
                              ../firmware-artifacts/esp32-s3-devkitc-1.bin
                            cp .pio/build/esp32-s3-real/firmware.bin \
                              ../firmware-artifacts/esp32-s3-real.bin
                            (cd ../firmware-artifacts && sha256sum ./*.bin > SHA256SUMS)
                        '''
                    }

                    post {
                        success {
                            archiveArtifacts(
                                artifacts: 'firmware-artifacts/*',
                                fingerprint: true
                            )
                        }
                    }
                }

                stage('Production deployment contract') {
                    steps {
                        sh '''
                            set -eu

                            shellcheck infra/mqtt/scripts/*.sh
                            shellcheck infra/mqtt/mosquitto/bootstrap.sh
                            shellcheck deploy/scripts/*.sh

                            temporary_directory="$(mktemp -d)"
                            trap 'rm -rf "${temporary_directory}"' EXIT
                            install -d \
                              "${temporary_directory}/auth" \
                              "${temporary_directory}/tls" \
                              "${temporary_directory}/data" \
                              "${temporary_directory}/log"
                            : > "${temporary_directory}/auth/passwd"
                            : > "${temporary_directory}/tls/tls.crt"
                            : > "${temporary_directory}/tls/tls.key"

                            MOSQUITTO_IMAGE='eclipse-mosquitto@sha256:0000000000000000000000000000000000000000000000000000000000000000' \
                            Mqtt__Username='backend-bridge' \
                            Mqtt__Password='ci-only-password' \
                            MQTT_AUTH_DIR="${temporary_directory}/auth" \
                            MQTT_TLS_DIR="${temporary_directory}/tls" \
                            MQTT_DATA_DIR="${temporary_directory}/data" \
                            MQTT_LOG_DIR="${temporary_directory}/log" \
                              docker compose \
                                -f infra/docker-compose.prod.yml \
                                config --quiet

                            awk \
                              -v output="${temporary_directory}" \
                              '/-----BEGIN CERTIFICATE-----/ {
                                 capture=1
                                 count++
                                 file=sprintf("%s/ca-%d.pem", output, count)
                               }
                               capture { print > file }
                               /-----END CERTIFICATE-----/ {
                                 close(file)
                                 capture=0
                               }
                               END { if (count != 2) exit 1 }' \
                              firmware-esp32/src/net/ca_cert_embedded.h

                            openssl x509 \
                              -in "${temporary_directory}/ca-1.pem" \
                              -noout -checkend 86400 >/dev/null
                            openssl x509 \
                              -in "${temporary_directory}/ca-2.pem" \
                              -noout -checkend 86400 >/dev/null

                            fingerprint="$(
                              openssl x509 \
                                -in "${temporary_directory}/ca-2.pem" \
                                -noout -fingerprint -sha256 |
                                cut -d= -f2
                            )"
                            test "${fingerprint}" = \
                              '96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6'
                        '''
                    }
                }

                stage('Filesystem security and SBOM') {
                    steps {
                        sh '''
                            set -eu

                            # PlatformIO materializes upstream build caches and development-only
                            # dependency metadata under .pio. Scan the repository inputs, not those
                            # generated files or the firmware binaries archived by the prior stage.
                            trivy fs \
                              --ignore-unfixed \
                              --exit-code 1 \
                              --severity HIGH,CRITICAL \
                              --scanners vuln,secret,misconfig \
                              --skip-dirs firmware-esp32/.pio \
                              --skip-dirs firmware-artifacts \
                              --format json \
                              --output trivy-iot-fs.json \
                              .
                            syft dir:. \
                              --exclude './firmware-esp32/.pio/**' \
                              --exclude './firmware-artifacts/**' \
                              -o cyclonedx-json=sbom-iot.cdx.json
                        '''
                    }
                    post {
                        always {
                            archiveArtifacts(
                                allowEmptyArchive: true,
                                artifacts: 'trivy-iot-fs.json,sbom-iot.cdx.json'
                            )
                        }
                    }
                }
            }

            post {
                always {
                    deleteDir()
                }
            }
        }

        stage('Request trusted production release') {
            when {
                allOf {
                    branch 'main'
                    expression { env.CHANGE_ID == null }
                }
            }
            steps {
                build(
                    job: 'solar-iot-production',
                    wait: true,
                    propagate: true,
                    parameters: [
                        string(name: 'GIT_SHA', value: env.GIT_SHA)
                    ]
                )
            }
        }
    }

    post {
        success {
            echo "IoT pipeline succeeded for ${env.GIT_SHA}"
        }
        failure {
            echo 'IoT pipeline failed; production was not changed or Docker rolled back'
        }
    }
}
