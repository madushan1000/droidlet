/*
 * Host settings provider.
 *
 * Settings.Global/Secure/System normally talk to the system_server
 * SettingsProvider over content://settings.  droidlet has no system_server, so
 * the launcher installs this provider into the app process alongside the
 * app's own manifest providers.  Values are process-local and reset with the
 * process; defaults cover the keys AndroidX libraries read during startup.
 */

package android.provider;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Bundle;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class DroidletSettingsProvider extends ContentProvider {
    public static final String AUTHORITY = "settings";

    private static final String NAME = "name";
    private static final String VALUE = "value";

    private final Map<String, String> mGlobal = new HashMap<>();
    private final Map<String, String> mSecure = new HashMap<>();
    private final Map<String, String> mSystem = new HashMap<>();

    @Override
    public boolean onCreate() {
        mSecure.put("android_id", "droidlet0000000000");
        mSecure.put("user_setup_complete", "1");
        mSecure.put("enabled_accessibility_services", "");
        mGlobal.put("adb_enabled", "1");
        mGlobal.put("device_provisioned", "1");
        mGlobal.put("stay_on_while_plugged_in", "0");
        mGlobal.put("airplane_mode_on", "0");
        mSystem.put("screen_brightness", "128");
        mSystem.put("screen_off_timeout", "60000");
        return true;
    }

    private Map<String, String> tableFor(Uri uri) {
        if (uri == null) {
            return null;
        }
        List<String> segments = uri.getPathSegments();
        if (segments.isEmpty()) {
            return null;
        }
        switch (segments.get(0)) {
            case "global":
                return mGlobal;
            case "secure":
                return mSecure;
            case "system":
                return mSystem;
            default:
                return null;
        }
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) {
        String[] columns = projection != null ? projection : new String[] {NAME, VALUE};
        MatrixCursor cursor = new MatrixCursor(columns);
        Map<String, String> table = tableFor(uri);
        String name = selectionArgs != null && selectionArgs.length > 0
                ? selectionArgs[0] : null;
        if (table == null || name == null) {
            return cursor;
        }
        String value = table.get(name);
        if (value == null) {
            return cursor;
        }
        Object[] row = new Object[columns.length];
        for (int i = 0; i < columns.length; i++) {
            if (NAME.equals(columns[i])) {
                row[i] = name;
            } else if (VALUE.equals(columns[i])) {
                row[i] = value;
            }
        }
        cursor.addRow(row);
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        return "vnd.android.cursor.dir/vnd.android.settings";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        Map<String, String> table = tableFor(uri);
        String name = values != null ? values.getAsString(NAME) : null;
        if (table != null && name != null) {
            table.put(name, values.getAsString(VALUE));
        }
        return uri;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        Map<String, String> table = tableFor(uri);
        if (table == null) {
            return 0;
        }
        int before = table.size();
        if (selectionArgs != null && selectionArgs.length > 0) {
            table.remove(selectionArgs[0]);
        } else {
            table.clear();
        }
        return before - table.size();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
            String[] selectionArgs) {
        insert(uri, values);
        return 1;
    }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        if (method == null) {
            return null;
        }
        if (method.startsWith("GET_")) {
            Map<String, String> table = tableForName(methodSuffix(method));
            Bundle result = new Bundle();
            if (table != null && arg != null) {
                result.putString(VALUE, table.get(arg));
            }
            return result;
        }
        if (method.startsWith("PUT_")) {
            Map<String, String> table = tableForName(methodSuffix(method));
            if (table != null && arg != null) {
                table.put(arg, extras != null ? extras.getString(VALUE) : null);
            }
            return new Bundle();
        }
        if (method.startsWith("DELETE_")) {
            Map<String, String> table = tableForName(methodSuffix(method));
            if (table != null && arg != null) {
                table.remove(arg);
            }
            return new Bundle();
        }
        if (method.startsWith("RESET_")) {
            Map<String, String> table = tableForName(methodSuffix(method));
            if (table != null) {
                table.clear();
            }
            return new Bundle();
        }
        if (method.startsWith("LIST_")) {
            Map<String, String> table = tableForName(methodSuffix(method));
            Bundle result = new Bundle();
            if (table != null) {
                result.putStringArrayList(VALUE, new ArrayList<>(table.keySet()));
            }
            return result;
        }
        return null;
    }

    private static String methodSuffix(String method) {
        int underscore = method.indexOf('_');
        return underscore >= 0 ? method.substring(underscore + 1) : "";
    }

    private Map<String, String> tableForName(String suffix) {
        switch (suffix) {
            case "global":
                return mGlobal;
            case "secure":
                return mSecure;
            case "system":
                return mSystem;
            default:
                return null;
        }
    }
}
