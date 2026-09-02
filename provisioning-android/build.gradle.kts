plugins {
    id("com.android.application") version "8.12.2" apply false
    // Chỉ khai báo, không apply ở đây. Module :app (Java thuần) không đụng tới hai plugin
    // này nên vẫn build y như trước.
    id("org.jetbrains.kotlin.android") version "2.1.20" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.20" apply false
}
