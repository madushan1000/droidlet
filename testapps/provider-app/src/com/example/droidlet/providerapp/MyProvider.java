package com.example.droidlet.providerapp;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.util.Log;

public class MyProvider extends ContentProvider {
    private static final String TAG = "DroidletProvider";
    public static final String AUTHORITY = "com.example.droidlet.provider";
    public static final Uri CONTENT_URI = Uri.parse("content://" + AUTHORITY + "/items");

    @Override
    public boolean onCreate() {
        Log.i(TAG, "Provider.onCreate");
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) {
        Log.i(TAG, "Provider.query " + uri);
        MatrixCursor cursor = new MatrixCursor(new String[] {"value"});
        cursor.addRow(new Object[] {"provider-ready"});
        return cursor;
    }

    @Override public String getType(Uri uri) { return "vnd.android.cursor.dir/vnd.droidlet.item"; }

    @Override public Uri insert(Uri uri, ContentValues values) { return uri; }

    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }

    @Override public int update(Uri uri, ContentValues values, String selection,
            String[] selectionArgs) { return 0; }
}
