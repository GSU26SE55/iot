package com.solarbms.setup;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
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

import androidx.activity.ComponentActivity;
import androidx.activity.OnBackPressedCallback;

import org.json.JSONObject;

import java.util.Arrays;

public final class MainActivity extends ComponentActivity {
    private static final String[] PORTAL_URLS = {
            "http://192.168.4.1:8080/",
            "http://solar-gateway.local:8080/",
            "http://solargw.local:8080/"
    };
    private static final String PORTAL_USER = "admin";
    private static final String PORTAL_PASSWORD = "12345678";
    private static final int CAMERA_PERMISSION_REQUEST = 41;
    private static final int QR_SCAN_REQUEST = 42;

    private WebView webView;
    private TextView connectionStatus;
    private TextView statusBadge;
    private View statusDot;
    private View portalEmptyState;
    private ProgressBar progressBar;
    private Button scanQrButton;
    private PermissionRequest pendingCameraRequest;
    private ConnectivityManager connectivityManager;
    private ConnectivityManager.NetworkCallback wifiNetworkCallback;
    private Network boundWifiNetwork;
    private boolean nativeScannerRequested;
    private boolean portalLoaded;
    private boolean mainFrameFailed;
    private boolean wifiPanelOpened;
    private int portalUrlIndex;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                        | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        );

        connectionStatus = findViewById(R.id.connectionStatus);
        statusBadge = findViewById(R.id.statusBadge);
        statusDot = findViewById(R.id.statusDot);
        portalEmptyState = findViewById(R.id.portalEmptyState);
        progressBar = findViewById(R.id.progressBar);
        webView = findViewById(R.id.portalWebView);
        scanQrButton = findViewById(R.id.scanQrButton);

        Button wifiButton = findViewById(R.id.wifiButton);
        Button reloadButton = findViewById(R.id.reloadButton);
        Button emptyWifiButton = findViewById(R.id.emptyWifiButton);
        Button emptyReloadButton = findViewById(R.id.emptyReloadButton);

        configureWebView();
        configureWifiRouting();
        wifiButton.setOnClickListener(view -> openWifiPanel());
        emptyWifiButton.setOnClickListener(view -> openWifiPanel());
        reloadButton.setOnClickListener(view -> loadPortal());
        emptyReloadButton.setOnClickListener(view -> loadPortal());
        scanQrButton.setOnClickListener(view -> requestNativeQrScan());
        getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
            @Override
            public void handleOnBackPressed() {
                if (webView.canGoBack()) {
                    webView.goBack();
                    return;
                }
                setEnabled(false);
                getOnBackPressedDispatcher().onBackPressed();
            }
        });

        setPortalStateConnecting();
        if (savedInstanceState == null) {
            loadPortal();
        } else {
            webView.restoreState(savedInstanceState);
        }
    }

    private void configureWebView() {
        WebView.setWebContentsDebuggingEnabled(false);
        webView.setOverScrollMode(View.OVER_SCROLL_NEVER);
        webView.setBackgroundColor(getColor(R.color.surface));

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setMediaPlaybackRequiresUserGesture(false);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setSaveFormData(false);
        settings.setCacheMode(WebSettings.LOAD_NO_CACHE);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setUserAgentString(
                settings.getUserAgentString() + " SolarBMSSetup/1.1"
        );
        for (String host : new String[]{
                "192.168.4.1", "solar-gateway.local", "solargw.local"
        }) {
            webView.setHttpAuthUsernamePassword(
                    host,
                    "Solar BMS Setup",
                    PORTAL_USER,
                    PORTAL_PASSWORD
            );
        }

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri uri = request.getUrl();
                if (isPortalUri(uri)) return false;
                Toast.makeText(
                        MainActivity.this,
                        R.string.portal_external_link_blocked,
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
                if (isPortalHost(host)) handler.proceed(PORTAL_USER, PORTAL_PASSWORD);
                else handler.cancel();
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                Uri uri = url == null ? null : Uri.parse(url);
                if (!mainFrameFailed && isPortalUri(uri)) setPortalStateConnected();
            }

            @Override
            public void onReceivedError(
                    WebView view,
                    WebResourceRequest request,
                    WebResourceError error
            ) {
                if (!request.isForMainFrame()) return;
                if (portalUrlIndex + 1 < PORTAL_URLS.length) {
                    portalUrlIndex++;
                    mainFrameFailed = false;
                    view.loadUrl(PORTAL_URLS[portalUrlIndex]);
                    return;
                }
                mainFrameFailed = true;
                setPortalStateUnavailable();
            }
        });

        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onProgressChanged(WebView view, int newProgress) {
                progressBar.setProgress(newProgress);
                progressBar.setVisibility(newProgress < 100 ? View.VISIBLE : View.INVISIBLE);
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

    private void configureWifiRouting() {
        connectivityManager = getSystemService(ConnectivityManager.class);
        if (connectivityManager == null) return;

        wifiNetworkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                bindToWifiNetwork(network);
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed() || portalLoaded) return;
                    webView.postDelayed(MainActivity.this::loadPortal, 350L);
                });
            }

            @Override
            public void onLost(Network network) {
                if (!network.equals(boundWifiNetwork)) return;
                connectivityManager.bindProcessToNetwork(null);
                boundWifiNetwork = null;
            }
        };

        NetworkRequest wifiRequest = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
        connectivityManager.registerNetworkCallback(wifiRequest, wifiNetworkCallback);
        bindToAvailableWifiNetwork();
    }

    private boolean bindToAvailableWifiNetwork() {
        if (connectivityManager == null) return false;
        for (Network network : connectivityManager.getAllNetworks()) {
            NetworkCapabilities capabilities =
                    connectivityManager.getNetworkCapabilities(network);
            if (capabilities != null
                    && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                return bindToWifiNetwork(network);
            }
        }
        return false;
    }

    private boolean bindToWifiNetwork(Network network) {
        if (connectivityManager == null || network == null) return false;
        if (!connectivityManager.bindProcessToNetwork(network)) return false;
        boundWifiNetwork = network;
        return true;
    }

    private void setPortalStateConnecting() {
        portalLoaded = false;
        connectionStatus.setText(R.string.portal_connecting_detail);
        statusBadge.setText(R.string.status_connecting);
        statusBadge.setTextColor(getColor(R.color.status_waiting_text));
        statusBadge.setBackgroundResource(R.drawable.bg_status_waiting);
        statusDot.setBackgroundResource(R.drawable.dot_waiting);
        scanQrButton.setEnabled(false);
        scanQrButton.setAlpha(0.48f);
        portalEmptyState.setVisibility(View.VISIBLE);
        webView.setVisibility(View.INVISIBLE);
    }

    private void setPortalStateConnected() {
        portalLoaded = true;
        mainFrameFailed = false;
        connectionStatus.setText(R.string.portal_connected);
        statusBadge.setText(R.string.status_connected);
        statusBadge.setTextColor(getColor(R.color.status_connected_text));
        statusBadge.setBackgroundResource(R.drawable.bg_status_connected);
        statusDot.setBackgroundResource(R.drawable.dot_connected);
        scanQrButton.setEnabled(true);
        scanQrButton.setAlpha(1f);
        portalEmptyState.setVisibility(View.GONE);
        webView.setVisibility(View.VISIBLE);
    }

    private void setPortalStateUnavailable() {
        portalLoaded = false;
        connectionStatus.setText(R.string.portal_not_found_detail);
        statusBadge.setText(R.string.status_not_connected);
        statusBadge.setTextColor(getColor(R.color.status_error_text));
        statusBadge.setBackgroundResource(R.drawable.bg_status_error);
        statusDot.setBackgroundResource(R.drawable.dot_error);
        scanQrButton.setEnabled(false);
        scanQrButton.setAlpha(0.48f);
        progressBar.setVisibility(View.INVISIBLE);
        portalEmptyState.setVisibility(View.VISIBLE);
        webView.setVisibility(View.INVISIBLE);
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

    private void requestNativeQrScan() {
        if (!portalLoaded) {
            Toast.makeText(this, R.string.scan_requires_portal, Toast.LENGTH_LONG).show();
            return;
        }
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            launchQrScanner();
            return;
        }
        nativeScannerRequested = true;
        requestPermissions(
                new String[]{Manifest.permission.CAMERA},
                CAMERA_PERMISSION_REQUEST
        );
    }

    private void launchQrScanner() {
        startActivityForResult(
                new Intent(this, QrScannerActivity.class),
                QR_SCAN_REQUEST
        );
    }

    private boolean isPortalUri(Uri uri) {
        if (uri == null || !"http".equalsIgnoreCase(uri.getScheme())) return false;
        int port = uri.getPort();
        return isPortalHost(uri.getHost()) && (port == -1 || port == 8080);
    }

    private boolean isPortalHost(String host) {
        return "192.168.4.1".equalsIgnoreCase(host)
                || "solargw.local".equalsIgnoreCase(host)
                || "solar-gateway.local".equalsIgnoreCase(host);
    }

    private void loadPortal() {
        bindToAvailableWifiNetwork();
        portalUrlIndex = 0;
        mainFrameFailed = false;
        setPortalStateConnecting();
        progressBar.setProgress(0);
        progressBar.setVisibility(View.VISIBLE);
        webView.loadUrl(PORTAL_URLS[portalUrlIndex]);
    }

    private void openWifiPanel() {
        connectionStatus.setText(R.string.open_wifi_instruction);
        statusBadge.setText(R.string.status_waiting_wifi);
        statusBadge.setTextColor(getColor(R.color.status_waiting_text));
        statusBadge.setBackgroundResource(R.drawable.bg_status_waiting);
        statusDot.setBackgroundResource(R.drawable.dot_waiting);
        wifiPanelOpened = true;

        Intent intent;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            intent = new Intent(Settings.Panel.ACTION_WIFI);
        } else {
            intent = new Intent(Settings.ACTION_WIFI_SETTINGS);
        }
        startActivity(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (wifiPanelOpened) {
            wifiPanelOpened = false;
            webView.postDelayed(this::loadPortal, 700L);
        }
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

        if (nativeScannerRequested) {
            nativeScannerRequested = false;
            if (granted) launchQrScanner();
            else Toast.makeText(this, R.string.camera_required, Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != QR_SCAN_REQUEST || resultCode != RESULT_OK || data == null) return;

        String qrValue = data.getStringExtra(QrScannerActivity.EXTRA_QR_VALUE);
        if (qrValue == null || qrValue.isEmpty()) return;
        if (!portalLoaded) {
            Toast.makeText(this, R.string.scan_result_portal_lost, Toast.LENGTH_LONG).show();
            return;
        }

        // Reuse the portal's parser and save flow so credentials are validated in one place.
        webView.evaluateJavascript("applyQr(" + JSONObject.quote(qrValue) + ")", null);
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        webView.saveState(outState);
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onDestroy() {
        if (pendingCameraRequest != null) pendingCameraRequest.deny();
        if (connectivityManager != null && wifiNetworkCallback != null) {
            try {
                connectivityManager.unregisterNetworkCallback(wifiNetworkCallback);
            } catch (IllegalArgumentException ignored) {
                // Callback was already unregistered by the system.
            }
            connectivityManager.bindProcessToNetwork(null);
        }
        webView.stopLoading();
        webView.destroy();
        super.onDestroy();
    }
}
