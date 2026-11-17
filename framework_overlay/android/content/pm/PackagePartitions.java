/*
 * Host-friendly replacement for android.content.pm.PackagePartitions.
 *
 * The framework version computes FINGERPRINT with
 * SystemProperties.digestOf(), which uses SHA-1 through the AndroidOpenSSL
 * provider.  droidlet does not build conscrypt's native crypto, so the digest
 * cannot be constructed and PackagePartitions fails to initialize during
 * AssetManager.getSystem().
 *
 * Only OverlayConfig consumes this class in an app process, and it needs the
 * partition list, not the fingerprint.  Keep the public shape but derive the
 * fingerprint from Build.FINGERPRINT instead of a digest.
 */

package android.content.pm;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.function.Function;

public class PackagePartitions {
    public static final int PARTITION_SYSTEM = 0;
    public static final int PARTITION_VENDOR = 1;
    public static final int PARTITION_ODM = 2;
    public static final int PARTITION_OEM = 3;
    public static final int PARTITION_PRODUCT = 4;
    public static final int PARTITION_SYSTEM_EXT = 5;

    private static final ArrayList<SystemPartition> SYSTEM_PARTITIONS =
            new ArrayList<>(Arrays.asList(
                    new SystemPartition(new File("/system"),
                            PARTITION_SYSTEM, "system", true, false),
                    new SystemPartition(new File("/vendor"),
                            PARTITION_VENDOR, "vendor", true, true),
                    new SystemPartition(new File("/odm"),
                            PARTITION_ODM, "odm", true, true),
                    new SystemPartition(new File("/oem"),
                            PARTITION_OEM, "oem", false, true),
                    new SystemPartition(new File("/product"),
                            PARTITION_PRODUCT, "product", true, true),
                    new SystemPartition(new File("/system_ext"),
                            PARTITION_SYSTEM_EXT, "system_ext", true, true)));

    public static final String FINGERPRINT = "droidlet-host";

    private PackagePartitions() {}

    public static <T> ArrayList<T> getOrderedPartitions(
            Function<SystemPartition, T> producer) {
        final ArrayList<T> out = new ArrayList<>();
        for (int i = 0, n = SYSTEM_PARTITIONS.size(); i < n; i++) {
            final T v = producer.apply(SYSTEM_PARTITIONS.get(i));
            if (v != null) {
                out.add(v);
            }
        }
        return out;
    }

    private static File canonicalize(File path) {
        try {
            return path.getCanonicalFile();
        } catch (IOException e) {
            return path;
        }
    }

    public static class SystemPartition {
        public final int type;

        private final String mName;
        private final DeferredCanonicalFile mFolder;
        private final DeferredCanonicalFile mAppFolder;
        private final DeferredCanonicalFile mPrivAppFolder;
        private final DeferredCanonicalFile mOverlayFolder;
        private final File mNonConicalFolder;

        private SystemPartition(File folder, int type, String name,
                boolean containsPrivApp, boolean containsOverlay) {
            this.type = type;
            this.mName = name;
            this.mFolder = new DeferredCanonicalFile(folder);
            this.mAppFolder = new DeferredCanonicalFile(folder, "app");
            this.mPrivAppFolder = containsPrivApp
                    ? new DeferredCanonicalFile(folder, "priv-app") : null;
            this.mOverlayFolder = containsOverlay
                    ? new DeferredCanonicalFile(folder, "overlay") : null;
            this.mNonConicalFolder = folder;
        }

        public SystemPartition(SystemPartition original) {
            this.type = original.type;
            this.mName = original.mName;
            this.mFolder = new DeferredCanonicalFile(original.mFolder.getFile());
            this.mAppFolder = original.mAppFolder;
            this.mPrivAppFolder = original.mPrivAppFolder;
            this.mOverlayFolder = original.mOverlayFolder;
            this.mNonConicalFolder = original.mNonConicalFolder;
        }

        public SystemPartition(File rootFolder, SystemPartition partition) {
            this(rootFolder, partition.type, partition.mName,
                    partition.mPrivAppFolder != null, partition.mOverlayFolder != null);
        }

        public String getName() {
            return mName;
        }

        public File getFolder() {
            return mFolder.getFile();
        }

        public File getNonConicalFolder() {
            return mNonConicalFolder;
        }

        public File getAppFolder() {
            return mAppFolder == null ? null : mAppFolder.getFile();
        }

        public File getPrivAppFolder() {
            return mPrivAppFolder == null ? null : mPrivAppFolder.getFile();
        }

        public File getOverlayFolder() {
            return mOverlayFolder == null ? null : mOverlayFolder.getFile();
        }

        public boolean containsPath(String path) {
            return containsFile(new File(path));
        }

        public boolean containsFile(File file) {
            return contains(mFolder.getFile(), canonicalize(file));
        }

        public boolean containsPrivApp(File scanFile) {
            return mPrivAppFolder != null
                    && contains(mPrivAppFolder.getFile(), canonicalize(scanFile));
        }

        public boolean containsApp(File scanFile) {
            return mAppFolder != null
                    && contains(mAppFolder.getFile(), canonicalize(scanFile));
        }

        public boolean containsOverlay(File scanFile) {
            return mOverlayFolder != null
                    && contains(mOverlayFolder.getFile(), canonicalize(scanFile));
        }

        private static boolean contains(File dir, File file) {
            if (dir == null || file == null) {
                return false;
            }
            final String dirPath = dir.getPath();
            final String filePath = file.getPath();
            return filePath.equals(dirPath)
                    || filePath.startsWith(dirPath.endsWith("/") ? dirPath : dirPath + "/");
        }
    }

    private static class DeferredCanonicalFile {
        private boolean mIsCanonical = false;
        private File mFile;

        private DeferredCanonicalFile(File dir) {
            mFile = dir;
        }

        private DeferredCanonicalFile(File dir, String fileName) {
            mFile = new File(dir, fileName);
        }

        private File getFile() {
            if (!mIsCanonical) {
                mFile = canonicalize(mFile);
                mIsCanonical = true;
            }
            return mFile;
        }
    }
}
