/*
 * droidlet network fixture: ConnectivityManager keeps the NetworkRequest that
 * the IConnectivityManager shim returns on its NetworkCallback and throws
 * IllegalArgumentException("NetworkCallback was not registered") from
 * unregisterNetworkCallback() when that request is null.  Glide's connectivity
 * monitor registers a default callback in onStart and unregisters it in
 * onStop, which used to abort Activity.onStop and kill the process.
 */

package com.example.droidlet.networkapp;

import android.app.Activity;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

public class MainActivity extends Activity {
    private static final String TAG = "NetworkApp";

    private final ConnectivityManager.NetworkCallback mDefault =
            new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network network) {
                    Log.i(TAG, "default onAvailable " + network);
                }

                @Override
                public void onLost(Network network) {
                    Log.i(TAG, "default onLost " + network);
                }
            };

    private final ConnectivityManager.NetworkCallback mRequested =
            new ConnectivityManager.NetworkCallback() {
                @Override
                public void onAvailable(Network network) {
                    Log.i(TAG, "requested onAvailable " + network);
                }

                @Override
                public void onLost(Network network) {
                    Log.i(TAG, "requested onLost " + network);
                }
            };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        ConnectivityManager cm =
                (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        Log.i(TAG, "ConnectivityManager created=" + (cm != null));
        try {
            cm.registerDefaultNetworkCallback(mDefault);
            Log.i(TAG, "default callback registered");

            NetworkRequest request = new NetworkRequest.Builder()
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .build();
            cm.registerNetworkCallback(request, mRequested);
            Log.i(TAG, "callback request registered");

            // This is the call that needs a non-null NetworkRequest from the
            // shim's requestNetwork()/listenForNetwork().
            cm.unregisterNetworkCallback(mRequested);
            cm.unregisterNetworkCallback(mDefault);
            Log.i(TAG, "callbacks unregistered");
        } catch (Throwable t) {
            Log.e(TAG, "connectivity callbacks failed", t);
        }

        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                finish();
            }
        }, 300);
    }
}
