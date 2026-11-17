/*
 * Host-friendly replacement for android.window.WindowExtensionsHelper.
 *
 * The framework version calls extensions.getClass() after getExtensions()
 * returns null when window extensions are not installed.  droidlet has no
 * androidx.window.extensions provider, so initEmbedding() crashed during
 * handleBindApplication().  Return false instead of dereferencing null.
 */

package android.window;

import android.content.Context;
import android.util.Log;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class WindowExtensionsHelper {
    private static final String TAG = "WindowExtensionsHelper";

    public static boolean initEmbedding(Context context) {
        try {
            final Object extensions = getExtensions(context);
            if (extensions == null) {
                Log.w(TAG, "Window extensions unavailable; skipping embedding init");
                return false;
            }

            final Method aeInitMethod = extensions.getClass()
                    .getDeclaredMethod("getActivityEmbeddingComponent");
            aeInitMethod.invoke(extensions);
            return true;
        } catch (InvocationTargetException | IllegalAccessException | NoSuchMethodException
                 | ClassNotFoundException e) {
            Log.w(TAG, "Failed to initialize embedding", e);
            return false;
        }
    }

    private static Object getExtensions(Context context) throws ClassNotFoundException,
            NoSuchMethodException, InvocationTargetException, IllegalAccessException {
        final Method isEnabled = Class.forName("android.view.WindowManager")
                .getDeclaredMethod("hasWindowExtensionsEnabled");
        isEnabled.setAccessible(true);
        if (!((Boolean) isEnabled.invoke(null))) {
            return null;
        }
        final Class<?> windowExtensionsProviderClass = Class.forName(
                "androidx.window.extensions.WindowExtensionsProvider",
                true /* initialize */, context.getClassLoader());
        final Method getWindowExtensionsMethod = windowExtensionsProviderClass
                .getDeclaredMethod("getWindowExtensions");
        return getWindowExtensionsMethod.invoke(null);
    }
}
