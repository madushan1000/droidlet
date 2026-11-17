/*
 * droidlet service fixture: binds a service that declares
 * android:process=":svc".  The service runs in a child droidlet process and
 * its binder arrives over binder RPC; the raw transaction below verifies that
 * a real binder call crosses the process boundary.
 */

package com.example.droidlet.serviceprocessapp;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.util.Log;

public class MainActivity extends Activity {
    private static final String TAG = "ServiceProcessApp";

    private boolean mBound;

    private final ServiceConnection mConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            Log.i(TAG, "onServiceConnected " + name.flattenToShortString());
            mBound = true;
            // Deliberately legacy AIDL style: Parcel.obtain() and a kernel
            // interface token.  The service host's compatibility layer must
            // convert this for binder RPC.
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            data.writeInterfaceToken("com.example.droidlet.serviceprocessapp.IRemote");
            try {
                data.writeInt(2);
                data.writeInt(3);
                boolean ok = binder.transact(RemoteService.TRANSACTION_ADD, data, reply, 0);
                Log.i(TAG, "transact=" + ok + " add=" + (ok ? reply.readInt() : -1));
            } catch (Throwable t) {
                Log.e(TAG, "binder call failed", t);
            } finally {
                data.recycle();
                reply.recycle();
            }
            new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
                @Override
                public void run() {
                    finishBinding();
                }
            }, 300);
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            Log.i(TAG, "onServiceDisconnected " + name.flattenToShortString());
            mBound = false;
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i(TAG, "onCreate");
        Intent intent = new Intent(this, RemoteService.class);
        if (!bindService(intent, mConnection, BIND_AUTO_CREATE)) {
            Log.e(TAG, "bindService returned false");
            finish();
            return;
        }
        // Safety net: never leave the host alive forever if the child fails.
        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                if (!mBound) {
                    Log.e(TAG, "binder did not arrive");
                    finishBinding();
                }
            }
        }, 15000);
    }

    private void finishBinding() {
        if (mBound) {
            unbindService(mConnection);
            mBound = false;
        }
        Log.i(TAG, "done");
        finish();
    }

    @Override
    protected void onDestroy() {
        Log.i(TAG, "onDestroy");
        super.onDestroy();
    }
}
