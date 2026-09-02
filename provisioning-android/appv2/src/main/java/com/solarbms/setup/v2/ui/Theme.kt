package com.solarbms.setup.v2.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

// Brand Solar BMS. Chỉ dùng khi máy không có dynamic color (API < 31).
private val Brand = Color(0xFF3559E8)
private val BrandDark = Color(0xFF9DB3FF)

private val LightScheme = lightColorScheme(
    primary = Brand,
    onPrimary = Color.White,
    primaryContainer = Color(0xFFE0E6FF),
    onPrimaryContainer = Color(0xFF001453),
    secondary = Color(0xFF5A5D72),
    surface = Color(0xFFFBF8FF),
    surfaceVariant = Color(0xFFE2E1EC),
    error = Color(0xFFBA1A1A),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
)

private val DarkScheme = darkColorScheme(
    primary = BrandDark,
    onPrimary = Color(0xFF002585),
    primaryContainer = Color(0xFF1B3BB8),
    onPrimaryContainer = Color(0xFFDDE1FF),
    secondary = Color(0xFFC3C5DD),
    surface = Color(0xFF121318),
    surfaceVariant = Color(0xFF45464F),
    error = Color(0xFFFFB4AB),
    errorContainer = Color(0xFF93000A),
    onErrorContainer = Color(0xFFFFDAD6),
)

/**
 * Material 3 thật, thay cho `android:Theme.Material.Light` (theme platform từ API 21) mà bản
 * cũ đang parent vào. Dark theme và dynamic color đều là mặc định của M3 — bản cũ không có
 * `values-night` nào nên bật dark mode là chữ đen trên nền đen.
 */
@Composable
fun SolarSetupTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val colorScheme = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)

        darkTheme -> DarkScheme
        else -> LightScheme
    }
    MaterialTheme(colorScheme = colorScheme, content = content)
}
