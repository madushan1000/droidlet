/*
 * droidlet fd-transfer fixture: binds a service in another droidlet process,
 * sends it a pipe read end through binder RPC, and reads the service's reply
 * pipe.  This exercises ParcelFileDescriptor with the RPC UNIX fd transport.
 */

package com.example.droidlet.servicefdapp;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends Activity {
    private static final String TAG = "ServiceFdApp";

    private boolean mBound;

    private final ServiceConnection mConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            Log.i(TAG, "onServiceConnected " + name.flattenToShortString());
            mBound = true;
            // Legacy AIDL style: unmarked parcels and a kernel interface
            // token; the compatibility layer rewrites them for binder RPC.
            Parcel data = Parcel.obtain();
            Parcel reply = Parcel.obtain();
            data.writeInterfaceToken("com.example.droidlet.servicefdapp.IFd");
            ParcelFileDescriptor readEnd = null;
            ParcelFileDescriptor response = null;
            try {
                Binder callback = new Binder() {
                    @Override
                    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                            throws android.os.RemoteException {
                        if (code == 42) {
                            reply.writeInt(99);
                            return true;
                        }
                        return super.onTransact(code, data, reply, flags);
                    }
                };
                ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
                OutputStream out = new FileOutputStream(pipe[1].getFileDescriptor());
                out.write("hello".getBytes("UTF-8"));
                out.close();
                readEnd = pipe[0];

                data.writeStrongBinder(callback);
                readEnd.writeToParcel(data, 0);
                boolean ok = binder.transact(FdService.TRANSACTION_PING, data, reply, 0);
                response = ok ? ParcelFileDescriptor.CREATOR.createFromParcel(reply) : null;
                String text = response == null ? null : readAll(response);
                Log.i(TAG, "transact=" + ok + " fd reply=" + text);
            } catch (Throwable t) {
                Log.e(TAG, "fd transaction failed", t);
            } finally {
                if (readEnd != null) {
                    try {
                        readEnd.close();
                    } catch (Throwable ignored) {
                    }
                }
                if (response != null) {
                    try {
                        response.close();
                    } catch (Throwable ignored) {
                    }
                }
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

    private static String readAll(ParcelFileDescriptor descriptor) throws Exception {
        InputStream in = new FileInputStream(descriptor.getFileDescriptor());
        byte[] buffer = new byte[64];
        int count = in.read(buffer);
        in.close();
        return count <= 0 ? "" : new String(buffer, 0, count, "UTF-8");
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i(TAG, "onCreate");
        Intent intent = new Intent(this, FdService.class);
        if (!bindService(intent, mConnection, BIND_AUTO_CREATE)) {
            Log.e(TAG, "bindService returned false");
            finish();
        }
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
