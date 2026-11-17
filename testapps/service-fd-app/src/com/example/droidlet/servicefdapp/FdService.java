package com.example.droidlet.servicefdapp;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class FdService extends Service {
    private static final String TAG = "ServiceFdApp";

    public static final int TRANSACTION_PING = 1;

    private final Binder mBinder = new Binder() {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (code == TRANSACTION_PING) {
                data.enforceInterface("com.example.droidlet.servicefdapp.IFd");
                IBinder callback = data.readStrongBinder();
                ParcelFileDescriptor received = ParcelFileDescriptor.CREATOR.createFromParcel(data);
                String text = received == null ? "<null>" : readAll(received);
                Log.i(TAG, "service received=" + text);
                if (callback != null) {
                    Parcel callbackData = Parcel.obtain();
                    Parcel callbackReply = Parcel.obtain();
                    try {
                        callbackData.writeInt(0);
                        callback.transact(42, callbackData, callbackReply, 0);
                        Log.i(TAG, "service callback=" + callbackReply.readInt());
                    } catch (Throwable t) {
                        Log.e(TAG, "service callback failed", t);
                    } finally {
                        callbackData.recycle();
                        callbackReply.recycle();
                    }
                }
                if (received != null) {
                    try {
                        received.close();
                    } catch (Throwable ignored) {
                    }
                }
                try {
                    ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
                    OutputStream out = new FileOutputStream(pipe[1].getFileDescriptor());
                    out.write(("echo:" + text).getBytes("UTF-8"));
                    out.close();
                    pipe[0].writeToParcel(reply, 0);
                    pipe[0].close();
                } catch (Throwable t) {
                    Log.e(TAG, "service reply pipe failed", t);
                    return false;
                }
                return true;
            }
            return super.onTransact(code, data, reply, flags);
        }
    };

    private static String readAll(ParcelFileDescriptor descriptor) {
        try {
            InputStream in = new FileInputStream(descriptor.getFileDescriptor());
            byte[] buffer = new byte[64];
            int count = in.read(buffer);
            in.close();
            return count <= 0 ? "" : new String(buffer, 0, count, "UTF-8");
        } catch (Throwable t) {
            Log.e(TAG, "service read failed", t);
            return "";
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "fd service onCreate pid=" + android.os.Process.myPid());
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "fd service onBind");
        return mBinder;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "fd service onDestroy");
        super.onDestroy();
    }
}
