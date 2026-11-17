package android.os;

import java.io.File;

/**
 * Host {@link Environment.UserEnvironment} that exposes a single external
 * storage directory rooted under droidlet's ANDROID_DATA tree.
 *
 * The platform normally obtains external volumes from StorageManager; there
 * is no storage service in the host build, so a NativeActivity's OBB lookup
 * would otherwise see a null volume list.
 */
public final class DroidletEnvironment extends Environment.UserEnvironment {
    private final File[] mExternalDirs;

    public DroidletEnvironment(int userId, File externalDir) {
        super(userId);
        mExternalDirs = new File[] { externalDir };
    }

    @Override
    public File[] getExternalDirs() {
        return mExternalDirs;
    }
}
