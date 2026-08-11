package com.solarbms.setup;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.webkit.HttpAuthHandler;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.util.Arrays;

public final class MainActivity extends Activity {
    private static final String PORTAL_URL = "http://192.168.4.1:8080/";
    private static final String PORTAL_USER = "admin";
    private static final String PORTAL_PASSWORD = "solar-setup-2026";
    private static final int CAMERA_PERMISSION_REQUEST = 41;
 Optimize, Optimize, Rating Optimize, Ratings Op Optimize, Ratings, Floyd.
    private WebView webView;
    private TextView connectionStatus;
    private ProgressBar progressBar;
    private PermissionRequest pendingCameraRequest;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(241, 246, 255));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        }
        setContentView(R.layout.activity_main);

        connectionStatus = findViewById(R.id.connectionStatus);
        progressBar = findViewById(R.id.progressBar);
        webView = findViewById(R.id.portalWebView);
        Button wifiButton = findViewById(R.id.wifiButton);
        Button reloadButton = findViewById(R.id.reloadButton);

        configureWebView();
        wifiButton.setOnClickListener(view -> openWifiPanel());
        reloadButton.setOnClickListener(view -> loadPortal());

        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(
                    new String[]{Manifest.permission.CAMERA},
                    CAMERA_PERMISSION_REQUEST
            );
        }

        if (savedInstanceState == null) {
            loadPortal();
        } else {
            webView.restoreState(savedInstanceState);
        }
    }

    private void configureWebView() {
        WebView.setWebContentsDebuggingEnabled(false);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setUserAgentString(
                settings.getUserAgentString() + " SolarBMSSetup/1.0"
        );

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri uri = request.getUrl();
                if (isPortalUri(uri)) return false;
                Toast.makeText(
                        MainActivity.this,
                        "App chỉ mở trang cấu hình của Solar Gateway.",
                        Toast.LENGTH_SHORT
                ).show();
                return true;
            }

            @Override
            public void onReceivedHttpAuthRequest(
                    WebView view,
                    HttpAuthHandler handler,
                    String host,
                    String realm
            ) {
                if (isPortalHost(host)) {
                    handler.proceed(PORTAL_USER, PORTAL_PASSWORD);
                } else {
                    handler.cancel();
                }
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                connectionStatus.setText(R.string.portal_connected);
            }

            @Override
            public void onReceivedError(
                    WebView view,
                    WebResourceRequest request,
                    WebResourceError error
            ) {
                if (request.isForMainFrame()) {
                    connectionStatus.setText(R.string.portal_not_found);
                    Toast.makeText(
                            MainActivity.this,
                            R.string.connect_gateway_hint,
                            Toast.LENGTH_LONG
                    ).show();
                }
            }
        });

        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onProgressChanged(WebView view, int newProgress) {
                progressBar.setProgress(newProgress);
                progressBar.setVisibility(newProgress < 100 ? View.VISIBLE : View.GONE);
            }

            @Override
            public void onPermissionRequest(PermissionRequest request) {
                runOnUiThread(() -> handleWebPermissionRequest(request));
            }

            @Override
            public void onPermissionRequestCanceled(PermissionRequest request) {
                if (pendingCameraRequest == request) pendingCameraRequest = null;
            }
        });
    }

    private void handleWebPermissionRequest(PermissionRequest request) {
        boolean wantsCamera = Arrays.asList(request.getResources())
                .contains(PermissionRequest.RESOURCE_VIDEO_CAPTURE);
        if (!isPortalUri(request.getOrigin()) || !wantsCamera) {
            request.deny();
            return;
        }

        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            request.grant(new String[]{PermissionRequest.RESOURCE_VIDEO_CAPTURE});
            return;
        }

        pendingCameraRequest = request;
        requestPermissions(
                new String[]{Manifest.permission.CAMERA},
                CAMERA_PERMISSION_REQUEST
        );
    }

    private boolean isPortalUri(Uri uri) {
        if (uri == null || !"http".equalsIgnoreCase(uri.getScheme())) return false;
        int port = uri.getPort();
        return isPortalHost(uri.getHost()) && (port == -1 || port == 8080);
    }

    private boolean isPortalHost(String host) {
        return "192.168.4.1".equalsIgnoreCase(host)
                || "solargw.local".equalsIgnoreCase(host);
    }

    private void loadPortal() {
        connectionStatus.setText(R.string.portal_connecting);
        webView.loadUrl(PORTAL_URL);
    }

    private void openWifiPanel() {
        Intent intent;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            intent = new Intent(Settings.Panel.ACTION_WIFI);
        } else {
            intent = new Intent(Settings.ACTION_WIFI_SETTINGS);
        }
        startActivity(intent);
    }

    @Override
    public void onRequestPermissionsResult(
            int requestCode,
            String[] permissions,
            int[] grantResults
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != CAMERA_PERMISSION_REQUEST) return;

        boolean granted = grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED;
        if (pendingCameraRequest != null) {
            if (granted) {
                pendingCameraRequest.grant(
                        new String[]{PermissionRequest.RESOURCE_VIDEO_CAPTURE}
                );
            } else {
                pendingCameraRequest.deny();
                Toast.makeText(this, R.string.camera_required, Toast.LENGTH_LONG).show();
            }
            pendingCameraRequest = null;
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        webView.saveState(outState);
        super.onSaveInstanceState(outState);
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        if (pendingCameraRequest != null) pendingCameraRequest.deny();
        webView.stopLoading();
        webView.destroy();
        super.onDestroy();
    }
}
