/*
 * Host replacement for android.content.pm.SystemFeaturesCache.
 *
 * In a normal Android process the SDK system-feature versions arrive through
 * ApplicationSharedMemory during bindApplication().  droidlet has no zygote or
 * app-shared-memory instance, so the framework's cache reports every SDK
 * feature as unavailable and ApplicationPackageManager never asks
 * IPackageManager.  Returning null here makes every query fall through to the
 * host PackageManager shim, which is the single source of truth for droidlet.
 */

package android.content.pm;

import android.annotation.NonNull;
import android.util.ArrayMap;

import java.util.Collection;

public final class SystemFeaturesCache {
    private static volatile SystemFeaturesCache sInstance;

    private final int[] mSdkFeatureVersions;

    public SystemFeaturesCache(@NonNull ArrayMap<String, ?> availableFeatures) {
        this(availableFeatures.values());
    }

    public SystemFeaturesCache(@NonNull Collection<?> availableFeatures) {
        mSdkFeatureVersions = new int[0];
    }

    public SystemFeaturesCache(@NonNull int[] sdkFeatureVersions) {
        mSdkFeatureVersions = sdkFeatureVersions;
    }

    public static void setInstance(SystemFeaturesCache instance) {
        sInstance = instance;
    }

    public static @NonNull SystemFeaturesCache getInstance() {
        SystemFeaturesCache instance = sInstance;
        if (instance == null) {
            instance = new SystemFeaturesCache(new int[0]);
            sInstance = instance;
        }
        return instance;
    }

    public static boolean hasInstance() {
        return sInstance != null;
    }

    public static void clearInstance() {
        sInstance = null;
    }

    public int[] getSdkFeatureVersions() {
        return mSdkFeatureVersions;
    }

    public Boolean maybeHasFeature(@NonNull String featureName, int version) {
        return null;
    }
}
