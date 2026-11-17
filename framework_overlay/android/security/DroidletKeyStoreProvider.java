/*
 * Software AndroidKeyStore compatibility provider.
 *
 * droidlet has no keystore daemon or TEE, so java.security.KeyStore
 * "AndroidKeyStore" is unavailable and apps that use flutter_secure_storage
 * fail before their first screen.  This provider implements the small subset
 * of the AndroidKeyStore API those apps rely on: an in-process key store,
 * AES key generation, and RSA/EC key-pair generation.  Keys are persisted in
 * the app's data directory.
 *
 * This is deliberately not a security boundary.  Keys are ordinary software
 * keys, not hardware-backed, and the store file is readable by the app.
 */

package android.security;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.spec.AlgorithmParameterSpec;
import java.security.Key;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.KeyStoreSpi;
import java.security.PrivateKey;
import java.security.Provider;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.UnrecoverableKeyException;
import java.security.cert.Certificate;
import java.security.cert.CertificateEncodingException;
import java.security.cert.CertificateException;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Collections;
import java.util.Date;
import java.util.Enumeration;
import java.util.LinkedHashMap;
import java.util.Map;

import javax.crypto.KeyGeneratorSpi;
import java.security.KeyPairGeneratorSpi;
import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;

public final class DroidletKeyStoreProvider extends Provider {
    public static final String NAME = "AndroidKeyStore";

    public DroidletKeyStoreProvider() {
        super(NAME, 1.0, "droidlet software AndroidKeyStore");
        put("KeyStore.AndroidKeyStore", DroidletKeyStoreSpi.class.getName());
        put("KeyGenerator.AES", AesKeyGenerator.class.getName());
        put("KeyPairGenerator.RSA", RsaKeyPairGenerator.class.getName());
        put("KeyPairGenerator.EC", EcKeyPairGenerator.class.getName());
    }

    private static final class Entry {
        Key key;
        Certificate certificate;
        Date created = new Date();
    }

    public static final class DroidletKeyStoreSpi extends KeyStoreSpi {
        private static final int MAGIC = 0x44524b53; // "DRKS"
        private static final Map<String, Entry> sEntries = new LinkedHashMap<>();
        private static boolean sLoaded;
        private static File sStoreFile;

        static synchronized void putKey(String alias, Key key, Certificate certificate) {
            loadLocked();
            Entry entry = new Entry();
            entry.key = key;
            entry.certificate = certificate;
            sEntries.put(alias, entry);
            saveLocked();
        }

        @Override
        public Key engineGetKey(String alias, char[] password)
                throws java.security.NoSuchAlgorithmException, UnrecoverableKeyException {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                return entry != null ? entry.key : null;
            }
        }

        @Override
        public Certificate[] engineGetCertificateChain(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                return entry != null && entry.certificate != null
                        ? new Certificate[] {entry.certificate} : null;
            }
        }

        @Override
        public Certificate engineGetCertificate(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                return entry != null ? entry.certificate : null;
            }
        }

        @Override
        public Date engineGetCreationDate(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                return entry != null ? entry.created : null;
            }
        }

        @Override
        public void engineSetKeyEntry(String alias, Key key, char[] password,
                Certificate[] chain) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = new Entry();
                entry.key = key;
                entry.certificate = chain != null && chain.length > 0 ? chain[0] : null;
                sEntries.put(alias, entry);
                saveLocked();
            }
        }

        @Override
        public void engineSetKeyEntry(String alias, byte[] key, Certificate[] chain)
                throws KeyStoreException {
            throw new KeyStoreException("encoded key entries are not supported");
        }

        @Override
        public void engineSetCertificateEntry(String alias, Certificate cert) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = new Entry();
                entry.certificate = cert;
                sEntries.put(alias, entry);
                saveLocked();
            }
        }

        @Override
        public void engineDeleteEntry(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                sEntries.remove(alias);
                saveLocked();
            }
        }

        @Override
        public Enumeration<String> engineAliases() {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                return Collections.enumeration(new java.util.ArrayList<>(sEntries.keySet()));
            }
        }

        @Override
        public boolean engineContainsAlias(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                return sEntries.containsKey(alias);
            }
        }

        @Override
        public int engineSize() {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                return sEntries.size();
            }
        }

        @Override
        public boolean engineIsKeyEntry(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                return entry != null && entry.key != null;
            }
        }

        @Override
        public String engineGetCertificateAlias(Certificate cert) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                for (Map.Entry<String, Entry> item : sEntries.entrySet()) {
                    if (item.getValue().certificate != null
                            && item.getValue().certificate.equals(cert)) {
                        return item.getKey();
                    }
                }
                return null;
            }
        }

        @Override
        public boolean engineIsCertificateEntry(String alias) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                return entry != null && entry.key == null && entry.certificate != null;
            }
        }

        @Override
        public KeyStore.Entry engineGetEntry(String alias, KeyStore.ProtectionParameter protParam)
                throws KeyStoreException {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                Entry entry = sEntries.get(alias);
                if (entry == null) {
                    return null;
                }
                if (entry.key instanceof PrivateKey) {
                    Certificate[] chain = entry.certificate != null
                            ? new Certificate[] {entry.certificate} : null;
                    return new KeyStore.PrivateKeyEntry((PrivateKey) entry.key, chain);
                }
                if (entry.key instanceof SecretKey) {
                    return new KeyStore.SecretKeyEntry((SecretKey) entry.key);
                }
                if (entry.certificate != null) {
                    return new KeyStore.TrustedCertificateEntry(entry.certificate);
                }
                return null;
            }
        }

        @Override
        public void engineSetEntry(String alias, KeyStore.Entry entry,
                KeyStore.ProtectionParameter protParam) throws KeyStoreException {
            if (entry instanceof KeyStore.PrivateKeyEntry) {
                KeyStore.PrivateKeyEntry privateEntry = (KeyStore.PrivateKeyEntry) entry;
                engineSetKeyEntry(alias, privateEntry.getPrivateKey(), null,
                        privateEntry.getCertificateChain());
            } else if (entry instanceof KeyStore.SecretKeyEntry) {
                engineSetKeyEntry(alias, ((KeyStore.SecretKeyEntry) entry).getSecretKey(),
                        null, null);
            } else if (entry instanceof KeyStore.TrustedCertificateEntry) {
                engineSetCertificateEntry(alias,
                        ((KeyStore.TrustedCertificateEntry) entry).getTrustedCertificate());
            } else {
                throw new KeyStoreException("unsupported key store entry");
            }
        }

        @Override
        public void engineLoad(InputStream stream, char[] password) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
            }
        }

        @Override
        public void engineLoad(KeyStore.LoadStoreParameter param) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
            }
        }

        @Override
        public void engineStore(OutputStream stream, char[] password) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                saveLocked();
            }
        }

        @Override
        public void engineStore(KeyStore.LoadStoreParameter param) {
            synchronized (DroidletKeyStoreSpi.class) {
                loadLocked();
                saveLocked();
            }
        }

        private static File storeFile() {
            if (sStoreFile != null) {
                return sStoreFile;
            }
            File dir = null;
            try {
                Class<?> activityThread = Class.forName("android.app.ActivityThread");
                Object application = activityThread.getMethod("currentApplication")
                        .invoke(null);
                if (application != null) {
                    dir = (File) application.getClass().getMethod("getDataDir")
                            .invoke(application);
                }
            } catch (Throwable ignored) {
            }
            if (dir == null) {
                String data = System.getenv("ANDROID_DATA");
                dir = new File(data != null ? data : System.getProperty("java.io.tmpdir"),
                        "droidlet-keystore");
            }
            dir.mkdirs();
            sStoreFile = new File(dir, "droidlet-keystore.bin");
            return sStoreFile;
        }

        private static void loadLocked() {
            if (sLoaded) {
                return;
            }
            sLoaded = true;
            File file = storeFile();
            if (!file.isFile()) {
                return;
            }
            try (DataInputStream in = new DataInputStream(
                    new BufferedInputStream(new FileInputStream(file)))) {
                if (in.readInt() != MAGIC) {
                    return;
                }
                int count = in.readInt();
                for (int i = 0; i < count; i++) {
                    String alias = in.readUTF();
                    Entry entry = new Entry();
                    String keyAlgorithm = readString(in);
                    byte[] keyBytes = readBytes(in);
                    if (keyBytes != null && keyAlgorithm != null) {
                        entry.key = decodeKey(keyAlgorithm, keyBytes);
                    }
                    String certAlgorithm = readString(in);
                    byte[] certBytes = readBytes(in);
                    if (certBytes != null && certAlgorithm != null) {
                        PublicKey publicKey = KeyFactory.getInstance(certAlgorithm)
                                .generatePublic(new X509EncodedKeySpec(certBytes));
                        entry.certificate = new DroidletCertificate(publicKey);
                    }
                    entry.created = new Date(in.readLong());
                    sEntries.put(alias, entry);
                }
            } catch (Throwable t) {
                System.err.println("droidlet-keystore: could not load " + file + ": " + t);
            }
        }

        private static void saveLocked() {
            File file = storeFile();
            File temp = new File(file.getPath() + ".tmp");
            try (DataOutputStream out = new DataOutputStream(
                    new BufferedOutputStream(new FileOutputStream(temp)))) {
                out.writeInt(MAGIC);
                out.writeInt(sEntries.size());
                for (Map.Entry<String, Entry> item : sEntries.entrySet()) {
                    Entry entry = item.getValue();
                    out.writeUTF(item.getKey());
                    if (entry.key != null) {
                        out.writeUTF(entry.key.getAlgorithm());
                        writeBytes(out, encodeKey(entry.key));
                    } else {
                        out.writeUTF("");
                        writeBytes(out, null);
                    }
                    PublicKey publicKey = entry.certificate != null
                            ? entry.certificate.getPublicKey() : null;
                    if (publicKey != null) {
                        out.writeUTF(publicKey.getAlgorithm());
                        writeBytes(out, publicKey.getEncoded());
                    } else {
                        out.writeUTF("");
                        writeBytes(out, null);
                    }
                    out.writeLong(entry.created.getTime());
                }
            } catch (Throwable t) {
                System.err.println("droidlet-keystore: could not save " + file + ": " + t);
                return;
            }
            if (!temp.renameTo(file)) {
                System.err.println("droidlet-keystore: could not replace " + file);
            }
        }

        private static byte[] encodeKey(Key key) {
            if (key instanceof PrivateKey) {
                return key.getEncoded();
            }
            return key.getEncoded();
        }

        private static Key decodeKey(String algorithm, byte[] bytes) throws Exception {
            if ("AES".equals(algorithm)) {
                return new SecretKeySpec(bytes, "AES");
            }
            return KeyFactory.getInstance(algorithm)
                    .generatePrivate(new PKCS8EncodedKeySpec(bytes));
        }

        private static String readString(DataInputStream in) throws Exception {
            String value = in.readUTF();
            return value.isEmpty() ? null : value;
        }

        private static byte[] readBytes(DataInputStream in) throws Exception {
            int length = in.readInt();
            if (length < 0) {
                return null;
            }
            byte[] bytes = new byte[length];
            in.readFully(bytes);
            return bytes;
        }

        private static void writeBytes(DataOutputStream out, byte[] bytes) throws Exception {
            if (bytes == null) {
                out.writeInt(-1);
            } else {
                out.writeInt(bytes.length);
                out.write(bytes);
            }
        }
    }

    private static final class DroidletCertificate extends Certificate {
        private final PublicKey mPublicKey;

        DroidletCertificate(PublicKey publicKey) {
            super("DROIDLET");
            mPublicKey = publicKey;
        }

        @Override
        public byte[] getEncoded() throws CertificateEncodingException {
            return mPublicKey.getEncoded();
        }

        @Override
        public void verify(PublicKey key) throws CertificateException {
        }

        @Override
        public void verify(PublicKey key, String sigProvider) throws CertificateException {
        }

        @Override
        public String toString() {
            return "droidlet software certificate for " + mPublicKey.getAlgorithm();
        }

        @Override
        public PublicKey getPublicKey() {
            return mPublicKey;
        }
    }

    private static final class ParameterSupport {
        static String alias(AlgorithmParameterSpec spec) {
            return stringMethod(spec, "getKeystoreAlias");
        }

        static int keySize(AlgorithmParameterSpec spec, int fallback) {
            Object value = objectMethod(spec, "getKeySize");
            return value instanceof Integer ? (Integer) value : fallback;
        }

        private static String stringMethod(Object target, String name) {
            Object value = objectMethod(target, name);
            return value instanceof String ? (String) value : null;
        }

        private static Object objectMethod(Object target, String name) {
            if (target == null) {
                return null;
            }
            try {
                return target.getClass().getMethod(name).invoke(target);
            } catch (Throwable ignored) {
                return null;
            }
        }
    }

    public static final class AesKeyGenerator extends KeyGeneratorSpi {
        private String mAlias;
        private int mKeySize = 256;

        @Override
        protected void engineInit(SecureRandom random) {
        }

        @Override
        protected void engineInit(AlgorithmParameterSpec params, SecureRandom random) {
            mAlias = ParameterSupport.alias(params);
            mKeySize = ParameterSupport.keySize(params, mKeySize);
        }

        @Override
        protected void engineInit(int keysize, SecureRandom random) {
            mKeySize = keysize;
        }

        @Override
        protected SecretKey engineGenerateKey() {
            int size = mKeySize > 0 ? mKeySize : 256;
            byte[] bytes = new byte[Math.max(16, size / 8)];
            new SecureRandom().nextBytes(bytes);
            SecretKey key = new SecretKeySpec(bytes, "AES");
            if (mAlias != null) {
                DroidletKeyStoreSpi.putKey(mAlias, key, null);
            }
            return key;
        }
    }

    public static final class RsaKeyPairGenerator extends KeyPairGeneratorSpi {
        private String mAlias;
        private int mKeySize = 2048;

        @Override
        public void initialize(int keysize, SecureRandom random) {
            mKeySize = keysize;
        }

        @Override
        public void initialize(AlgorithmParameterSpec params, SecureRandom random) {
            mAlias = ParameterSupport.alias(params);
            mKeySize = ParameterSupport.keySize(params, mKeySize);
        }

        @Override
        public KeyPair generateKeyPair() {
            try {
                KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
                generator.initialize(mKeySize > 0 ? mKeySize : 2048, new SecureRandom());
                KeyPair pair = generator.generateKeyPair();
                if (mAlias != null) {
                    DroidletKeyStoreSpi.putKey(mAlias, pair.getPrivate(),
                            new DroidletCertificate(pair.getPublic()));
                }
                return pair;
            } catch (Exception e) {
                throw new RuntimeException("unable to generate RSA key pair", e);
            }
        }
    }

    public static final class EcKeyPairGenerator extends KeyPairGeneratorSpi {
        private String mAlias;
        private int mKeySize = 256;

        @Override
        public void initialize(int keysize, SecureRandom random) {
            mKeySize = keysize;
        }

        @Override
        public void initialize(AlgorithmParameterSpec params, SecureRandom random) {
            mAlias = ParameterSupport.alias(params);
            mKeySize = ParameterSupport.keySize(params, mKeySize);
        }

        @Override
        public KeyPair generateKeyPair() {
            try {
                KeyPairGenerator generator = KeyPairGenerator.getInstance("EC");
                generator.initialize(mKeySize > 0 ? mKeySize : 256, new SecureRandom());
                KeyPair pair = generator.generateKeyPair();
                if (mAlias != null) {
                    DroidletKeyStoreSpi.putKey(mAlias, pair.getPrivate(),
                            new DroidletCertificate(pair.getPublic()));
                }
                return pair;
            } catch (Exception e) {
                throw new RuntimeException("unable to generate EC key pair", e);
            }
        }
    }
}
