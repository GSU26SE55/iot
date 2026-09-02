plugins {
    id("com.android.application")
}

android {
    namespace = "com.solarbms.setup"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.solarbms.setup"
        minSdk = 26
        targetSdk = 36
        versionCode = 3
        versionName = "1.1.1"
    }

    buildTypes {
        release {
            // ponytail: ký bằng debug keystore. APK này chỉ phát cho kỹ thuật viên và phải
            // cài đè được bản debug đang có trên máy (cùng chữ ký). Tạo keystore riêng khi
            // nào phát hành ra ngoài, không sớm hơn.
            signingConfig = signingConfigs.getByName("debug")
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation("androidx.activity:activity:1.10.1")
    implementation("androidx.camera:camera-camera2:1.4.2")
    implementation("androidx.camera:camera-lifecycle:1.4.2")
    implementation("androidx.camera:camera-view:1.4.2")
    implementation("com.google.mlkit:barcode-scanning:17.3.0")
}
