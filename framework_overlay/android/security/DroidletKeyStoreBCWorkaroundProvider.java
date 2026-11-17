/*
 * Software AndroidKeyStore JCE bridge.
 *
 * flutter_secure_storage asks for Cipher "RSA/ECB/OAEPPadding" from a provider
 * named AndroidKeyStoreBCWorkaround (AOSP uses this to avoid a BouncyCastle
 * padding bug).  droidlet has no such provider, so this class forwards the
 * requested transformations to whichever host JCE provider implements them.
 * The keys themselves come from DroidletKeyStoreProvider, so wrap and unwrap
 * stay inside the same process.
 */

package android.security;

import java.security.AlgorithmParameters;
import java.security.spec.AlgorithmParameterSpec;
import java.security.Key;
import java.security.Provider;
import java.security.SecureRandom;
import java.security.Security;

import javax.crypto.Cipher;
import javax.crypto.CipherSpi;
import javax.crypto.NoSuchPaddingException;

public final class DroidletKeyStoreBCWorkaroundProvider extends Provider {
    public static final String NAME = "AndroidKeyStoreBCWorkaround";

    public DroidletKeyStoreBCWorkaroundProvider() {
        super(NAME, 1.0, "droidlet AndroidKeyStore JCE bridge");
        put("Cipher.AES/GCM/NoPadding", AesGcm.class.getName());
        put("Cipher.AES/CBC/PKCS7Padding", AesCbcPkcs7.class.getName());
        put("Cipher.RSA/ECB/OAEPPadding", RsaOaep.class.getName());
    }

    private abstract static class DelegateCipherSpi extends CipherSpi {
        private final Cipher mDelegate;

        DelegateCipherSpi(String transformation, String fallback) {
            mDelegate = findCipher(transformation, fallback);
        }

        private static Cipher findCipher(String transformation, String fallback) {
            for (Provider provider : Security.getProviders()) {
                String name = provider.getName();
                if (name.startsWith("AndroidKeyStore")) {
                    continue;
                }
                try {
                    return Cipher.getInstance(transformation, provider);
                } catch (Throwable ignored) {
                }
            }
            if (fallback != null) {
                for (Provider provider : Security.getProviders()) {
                    String name = provider.getName();
                    if (name.startsWith("AndroidKeyStore")) {
                        continue;
                    }
                    try {
                        return Cipher.getInstance(fallback, provider);
                    } catch (Throwable ignored) {
                    }
                }
            }
            try {
                return Cipher.getInstance(transformation);
            } catch (Throwable ignored) {
            }
            return null;
        }

        private Cipher delegate() {
            if (mDelegate == null) {
                throw new IllegalStateException("no host JCE implementation available");
            }
            return mDelegate;
        }

        @Override
        protected void engineSetMode(String mode) throws java.security.NoSuchAlgorithmException {
            delegate().getProvider();
        }

        @Override
        protected void engineSetPadding(String padding) throws NoSuchPaddingException {
            delegate().getProvider();
        }

        @Override
        protected byte[] engineGetIV() {
            return delegate().getIV();
        }

        @Override
        protected AlgorithmParameters engineGetParameters() {
            return delegate().getParameters();
        }

        @Override
        protected void engineInit(int opmode, Key key, AlgorithmParameters params,
                SecureRandom random)
                throws java.security.InvalidKeyException,
                       java.security.InvalidAlgorithmParameterException {
            delegate().init(opmode, key, params, random);
        }

        @Override
        protected void engineInit(int opmode, Key key, SecureRandom random)
                throws java.security.InvalidKeyException {
            delegate().init(opmode, key, random);
        }

        @Override
        protected void engineInit(int opmode, Key key, AlgorithmParameterSpec params,
                SecureRandom random)
                throws java.security.InvalidKeyException,
                       java.security.InvalidAlgorithmParameterException {
            try {
                delegate().init(opmode, key, params, random);
            } catch (java.security.InvalidAlgorithmParameterException e) {
                // The fallback provider may only implement PKCS#1 padding and
                // reject OAEPParameterSpec.  Both wrap and unwrap use this
                // provider, so dropping the parameters keeps them symmetric.
                if (params == null) {
                    throw e;
                }
                delegate().init(opmode, key, random);
            }
        }

        @Override
        protected byte[] engineUpdate(byte[] input, int inputOffset, int inputLen) {
            return delegate().update(input, inputOffset, inputLen);
        }

        @Override
        protected int engineUpdate(byte[] input, int inputOffset, int inputLen, byte[] output,
                int outputOffset) throws javax.crypto.ShortBufferException {
            return delegate().update(input, inputOffset, inputLen, output, outputOffset);
        }

        @Override
        protected byte[] engineDoFinal(byte[] input, int inputOffset, int inputLen)
                throws javax.crypto.IllegalBlockSizeException,
                       javax.crypto.BadPaddingException {
            return delegate().doFinal(input, inputOffset, inputLen);
        }

        @Override
        protected int engineDoFinal(byte[] input, int inputOffset, int inputLen, byte[] output,
                int outputOffset)
                throws javax.crypto.ShortBufferException,
                       javax.crypto.IllegalBlockSizeException,
                       javax.crypto.BadPaddingException {
            return delegate().doFinal(input, inputOffset, inputLen, output, outputOffset);
        }

        @Override
        protected int engineGetBlockSize() {
            return delegate().getBlockSize();
        }

        @Override
        protected int engineGetOutputSize(int inputLen) {
            return delegate().getOutputSize(inputLen);
        }

        @Override
        protected int engineGetKeySize(Key key)
                throws java.security.InvalidKeyException {
            return 256;
        }

        @Override
        protected byte[] engineWrap(Key key)
                throws javax.crypto.IllegalBlockSizeException,
                       java.security.InvalidKeyException {
            return delegate().wrap(key);
        }

        @Override
        protected Key engineUnwrap(byte[] wrappedKey, String wrappedKeyAlgorithm,
                int wrappedKeyType)
                throws java.security.InvalidKeyException,
                       java.security.NoSuchAlgorithmException {
            return delegate().unwrap(wrappedKey, wrappedKeyAlgorithm, wrappedKeyType);
        }
    }

    public static final class AesGcm extends DelegateCipherSpi {
        public AesGcm() {
            super("AES/GCM/NoPadding", null);
        }
    }

    public static final class AesCbcPkcs7 extends DelegateCipherSpi {
        public AesCbcPkcs7() {
            super("AES/CBC/PKCS5Padding", null);
        }
    }

    public static final class RsaOaep extends DelegateCipherSpi {
        public RsaOaep() {
            super("RSA/ECB/OAEPPadding", "RSA/ECB/PKCS1Padding");
        }
    }
}
