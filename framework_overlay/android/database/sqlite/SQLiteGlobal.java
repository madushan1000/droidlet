/*
 * Host replacement for android.database.sqlite.SQLiteGlobal.
 *
 * The framework version computes the default SQLite page size with
 * new StatFs("/data").  droidlet runs from an install tree and does not have a
 * /data mount, so that throws IllegalArgumentException as soon as an app opens
 * a database.  Use the normal Android defaults instead; debug properties are
 * not consulted.
 */

package android.database.sqlite;

public final class SQLiteGlobal {
    public static final String SYNC_MODE_FULL = "FULL";

    static final String WIPE_CHECK_FILE_SUFFIX = "-wipecheck";

    public static volatile String sDefaultSyncMode;

    private static native int nativeReleaseMemory();

    private SQLiteGlobal() {
    }

    public static int releaseMemory() {
        return nativeReleaseMemory();
    }

    public static int getDefaultPageSize() {
        return 4096;
    }

    public static String getDefaultJournalMode() {
        return "TRUNCATE";
    }

    public static int getJournalSizeLimit() {
        return 524288;
    }

    public static String getDefaultSyncMode() {
        String mode = sDefaultSyncMode;
        return mode != null ? mode : "FULL";
    }

    public static String getWALSyncMode() {
        String mode = sDefaultSyncMode;
        return mode != null ? mode : "NORMAL";
    }

    public static int getWALAutoCheckpoint() {
        return 100;
    }

    public static int getWALConnectionPoolSize() {
        return 4;
    }

    public static int getIdleConnectionTimeout() {
        return 30000;
    }

    public static long getWALTruncateSize() {
        return 1048576;
    }

    public static boolean checkDbWipe() {
        return false;
    }
}
