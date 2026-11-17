package com.example.droidlet.serviceprocessapp;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;

public class RemoteService extends Service {
    private static final String TAG = "ServiceProcessApp";

    public static final int TRANSACTION_ADD = 1;

    private final Binder mBinder = new Binder() {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (code == TRANSACTION_ADD) {
                data.enforceInterface("com.example.droidlet.serviceprocessapp.IRemote");
                int left = data.readInt();
                int right = data.readInt();
                Log.i(TAG, "remote service onTransact add " + left + "+" + right);
                reply.writeInt(left + right);
                return true;
            }
            return super.onTransact(code, data, reply, flags);
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "remote service onCreate pid=" + android.os.Process.myPid());
    }

    @Override
    public IBinder onBind(Intent intent) {
        Log.i(TAG, "remote service onBind");
        return mBinder;
    }

    @Override
    public boolean onUnbind(Intent intent) {
        Log.i(TAG, "remote service onUnbind");
        return false;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "remote service onDestroy");
        super.onDestroy();
    }
}
