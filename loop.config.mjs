/**
 * loop-engine — cấu hình cho repo iot (firmware ESP32 + simulator).
 *
 * Mục đích: sửa nhóm issue firmware của milestone "E2E" (giao trên tracker của repo backend
 * nhưng code nằm ở đây): #735–#741, #745–#749, #809, #832, #835, #858, #860, #867, #868,
 * #870, #881–#884, #891, #903, #904, #914, #921, #923, #924, #936, #952.
 *
 * Thang bám ĐÚNG những gì CI của repo này chạy (.github/workflows/firmware-ci.yml) —
 * không phát minh lệnh mới.
 */

const FW = 'firmware-esp32';

export default {
  version: 1,
  id: 'iot-e2e-milestone',
  kind: 'generic',
  root: '.',
  workdir: '.loop',

  context: {
    constitution: [
      'loop/context/constitution.md',
      'loop/context/glossary.md',
      'loop/context/conventions.md',
    ],
    docs: [],
    map: {
      generator: 'bash .loop/gen/map.sh',
      output: '.loop/cache/map.md',
      ttlSeconds: 3600,
      timeout: 180000,
    },
    taskDir: 'loop/tasks',
    maxTokens: 60000,
    targetUtilisation: 0.5,
    includeGitStatus: true,
    includeDiff: true,
    diffMaxLines: 400,
  },

  runtime: {
    up: null,
    health: null,
    seed: null,
    down: null,
    baseUrl: null,
    env: {},

    // Chứng minh toolchain chạy được TRƯỚC khi tiêu vòng lặp. Thiếu config.h là lỗi
    // MÔI TRƯỜNG (CI tự copy từ config.example.h), không phải firmware sai.
    verify: [
      'pio --version',
      `test -f ${FW}/include/config.h || cp ${FW}/include/config.example.h ${FW}/include/config.h`,
    ],
  },

  // ───────────────────────────────────────────────────────────────────────
  // Thang: L0 test native (nhanh, không cần phần cứng) → L1 compile 2 env.
  //
  // CỐ Ý ĐẢO so với thói quen "build trước, test sau": `pio test -e native` chỉ mất ~12s và
  // bắt được lỗi logic, trong khi compile firmware phải qua toolchain chéo. Đặt test trước
  // cho phản hồi nhanh hơn; compile vẫn luôn chạy ở tầng kế.
  // ───────────────────────────────────────────────────────────────────────
  verifiers: [
    {
      id: 'native-test',
      level: 0,
      cmd: `cd ${FW} && pio test -e native`,
      // Unity in "N test cases: X succeeded" — không phải JUnit/TAP chuẩn, nên dùng exitcode:
      // đúng và không bao giờ parse sai.
      adapter: 'exitcode',
      timeout: 900000,
    },
    {
      // GH-738 — simulator là Python, không nằm trong thang PlatformIO. Dùng unittest của
      // thư viện chuẩn để không phải thêm phụ thuộc chỉ để chạy test.
      id: 'simulator-test',
      level: 0,
      cmd: "python3 -m unittest discover -s tools/simulator -p 'test_*.py'",
      adapter: 'exitcode',
      timeout: 300000,
    },
    {
      id: 'build-devkit',
      level: 1,
      cmd: `cd ${FW} && pio run -e esp32-s3-devkitc-1`,
      adapter: 'exitcode',
      timeout: 1800000,
    },
    {
      id: 'build-real',
      level: 1,
      // env dùng BMS thật — #884 nói env này undef cờ real-BMS. Phải luôn compile được.
      cmd: `cd ${FW} && pio run -e esp32-s3-real`,
      adapter: 'exitcode',
      timeout: 1800000,
    },
  ],

  // Khoá thước đo + luật chơi. KHÔNG khoá `test/**`: nhiều issue yêu cầu THÊM test, mà guard
  // coi việc thêm file trong vùng bất biến cũng là tamper và sẽ xoá.
  immutable: [
    'loop.config.mjs',
    'loop/context/**',
    'loop/rubrics/**',
    '.github/**',
    'firmware-esp32/platformio.ini',
  ],

  budget: {
    maxIterations: 8,
    maxTokens: null,
    maxUsd: null,
    maxWallClockMinutes: 120,
    maxConsecutiveNoProgress: 3,
  },

  flaky: { enabled: true, reruns: 1, quarantine: [] },

  actor: {
    provider: 'claude',
    model: null,
    extraArgs: ['--permission-mode', 'acceptEdits'],
    promptVia: 'file',
    timeout: 3600000,
    env: {},
  },

  judge: {
    enabled: true,
    provider: null,
    rubric: 'loop/rubrics/spec-compliance.md',
    timeout: 900000,
  },

  // Triệu chứng "toolchain chưa khởi động" — để agent không đi sửa firmware cho sự cố môi trường.
  environmentBreakSigns: [
    'PlatformIO Core',
    'Please install',
    'config.h: No such file',
    'Error: Unknown environment names',
    'UnknownPackageError',
  ],

  hooks: {
    beforeIteration: null,
    afterIteration: null,
    onGreen: null, // KHÔNG commit tự động — người dùng tự commit.
    onEscalate: null,
  },

  report: { keepRuns: 200 },
};
