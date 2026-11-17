/*
 * droidlet WebView probe: reports whether the host exposes a usable WebView
 * provider, then tries to create one and load a data: URL.
 */

package com.example.droidlet.webviewapp;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public class MainActivity extends Activity {
    private static final String TAG = "WebViewApp";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        boolean feature = getPackageManager()
                .hasSystemFeature(PackageManager.FEATURE_WEBVIEW);
        Log.i(TAG, "hasSystemFeature(webview)=" + feature);
        try {
            Object pkg = WebView.getCurrentWebViewPackage();
            Log.i(TAG, "current provider=" + pkg);
        } catch (Throwable t) {
            Log.i(TAG, "getCurrentWebViewPackage failed: " + t);
        }

        WebView webView;
        try {
            webView = new WebView(this);
            Log.i(TAG, "WebView created");
        } catch (Throwable t) {
            Log.e(TAG, "WebView creation failed: " + t);
            Log.e(TAG, Log.getStackTraceString(t));
            finishDelayed(1500);
            return;
        }

        setContentView(webView);
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                Log.i(TAG, "onPageFinished url=" + url);
                view.evaluateJavascript("document.body.innerText",
                        new android.webkit.ValueCallback<String>() {
                            @Override
                            public void onReceiveValue(String value) {
                                Log.i(TAG, "page text=" + value);
                            }
                        });
            }
        });
        webView.loadDataWithBaseURL("https://example.com/",
                "<html><body><h1>droidlet webview</h1>"
                        + "<script>document.body.appendChild("
                        + "document.createTextNode(' js-ok'));</script></body></html>",
                "text/html", "utf-8", null);
        finishDelayed(5000);
    }

    private void finishDelayed(long millis) {
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                finish();
            }
        }, millis);
    }
}
