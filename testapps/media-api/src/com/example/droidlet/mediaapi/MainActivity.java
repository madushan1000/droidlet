package com.example.droidlet.mediaapi;

import android.app.Activity;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity {
    private static final String TAG = "MediaApiTest";
    private static final int SAMPLE_RATE = 48000;
    private TextView mStatus;
    private TextView mExtractor;
    private SurfaceView mSurfaceView;
    private AudioTrack mTrack;
    private Thread mCodecThread;
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);
        mStatus = findViewById(R.id.status);
        mExtractor = findViewById(R.id.extractor);
        mSurfaceView = findViewById(R.id.surface);
        Button play = findViewById(R.id.play);
        play.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                playTone();
            }
        });
        testExtractor();
        mSurfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                startCodec(holder.getSurface());
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        try {
            playTone();
        } catch (Throwable t) {
            Log.e(TAG, "playTone failed", t);
        }
    }

    private void playTone() {
        stopTrack();
        int frames = SAMPLE_RATE * 2;
        short[] samples = new short[frames * 2];
        for (int i = 0; i < frames; i++) {
            short value = (short) (Math.sin(2.0 * Math.PI * 440.0 * i / SAMPLE_RATE) * 8000);
            samples[i * 2] = value;
            samples[i * 2 + 1] = value;
        }
        int minBuffer = AudioTrack.getMinBufferSize(SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        int bufferSize = Math.max(minBuffer, samples.length * 2);
        Log.i(TAG, "AudioTrack minBuffer=" + minBuffer + " bufferSize=" + bufferSize);
        AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
        AudioFormat format = new AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .build();
        AudioTrack track = new AudioTrack.Builder()
                .setAudioAttributes(attributes)
                .setAudioFormat(format)
                .setTransferMode(AudioTrack.MODE_STATIC)
                .setBufferSizeInBytes(bufferSize)
                .build();
        if (track.getState() != AudioTrack.STATE_INITIALIZED
                && track.getState() != AudioTrack.STATE_NO_STATIC_DATA) {
            mStatus.setText("AudioTrack init failed state=" + track.getState());
            Log.w(TAG, "AudioTrack init failed state=" + track.getState());
            track.release();
            return;
        }
        int written = track.write(samples, 0, samples.length);
        track.play();
        mTrack = track;
        mStatus.setText("AudioTrack playing: wrote " + written + " shorts, state="
                + track.getPlayState());
        Log.i(TAG, "AudioTrack playing: wrote " + written + " shorts, rate="
                + track.getSampleRate() + " channels=" + track.getChannelCount());
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (mTrack != null) {
                    mStatus.setText("AudioTrack done: position=" + mTrack.getPlaybackHeadPosition());
                    Log.i(TAG, "AudioTrack position=" + mTrack.getPlaybackHeadPosition());
                }
            }
        }, 2500);
    }

    private void testEncoder() {
        testAudioEncoder();
        testVideoEncoder();
    }

    private void testAudioEncoder() {
        final int sampleRate = 44100;
        final int totalSamples = sampleRate / 2;
        MediaCodec codec = null;
        try {
            MediaFormat format = MediaFormat.createAudioFormat("audio/mp4a-latm",
                    sampleRate, 1);
            format.setInteger(MediaFormat.KEY_BIT_RATE, 64000);
            codec = MediaCodec.createEncoderByType("audio/mp4a-latm");
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();
            short[] pcm = new short[totalSamples];
            for (int i = 0; i < totalSamples; i++) {
                pcm[i] = (short) (Math.sin(2.0 * Math.PI * 440.0 * i / sampleRate) * 8000);
            }
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inputDone = false;
            boolean outputDone = false;
            int inputOffset = 0;
            long ptsUs = 0;
            int encodedBytes = 0;
            int configBytes = 0;
            while (!outputDone) {
                if (!inputDone) {
                    int inputIndex = codec.dequeueInputBuffer(10000);
                    if (inputIndex >= 0) {
                        ByteBuffer input = codec.getInputBuffer(inputIndex);
                        input.clear();
                        int count = Math.min(1024, totalSamples - inputOffset);
                        if (count <= 0) {
                            codec.queueInputBuffer(inputIndex, 0, 0, ptsUs,
                                    MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            for (int i = 0; i < count; i++) {
                                input.putShort(pcm[inputOffset + i]);
                            }
                            codec.queueInputBuffer(inputIndex, 0, count * 2, ptsUs, 0);
                            inputOffset += count;
                            ptsUs += (long) count * 1000000L / sampleRate;
                        }
                    }
                }
                int outputIndex = codec.dequeueOutputBuffer(info, 10000);
                if (outputIndex >= 0) {
                    if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                        configBytes += info.size;
                        MediaFormat outputFormat = codec.getOutputFormat();
                        Log.i(TAG, "AAC encoder config=" + info.size
                                + " csd=" + outputFormat.containsKey("csd-0"));
                    } else {
                        encodedBytes += info.size;
                    }
                    codec.releaseOutputBuffer(outputIndex, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        outputDone = true;
                    }
                }
            }
            Log.i(TAG, "MediaCodec encoded audio: " + encodedBytes
                    + " bytes config=" + configBytes);
        } catch (Throwable t) {
            Log.e(TAG, "audio encoder failed", t);
        } finally {
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Throwable ignored) {
                }
                codec.release();
            }
        }
    }

    private void testVideoEncoder() {
        final int width = 320;
        final int height = 240;
        final int frameRate = 30;
        MediaCodec codec = null;
        try {
            MediaFormat format = MediaFormat.createVideoFormat("video/avc", width, height);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, 19 /* YUV420Planar */);
            format.setInteger(MediaFormat.KEY_BIT_RATE, 500000);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, frameRate);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            codec = MediaCodec.createEncoderByType("video/avc");
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            int encodedBytes = 0;
            int configBytes = 0;
            for (int frame = 0; frame < 10; frame++) {
                int inputIndex = -1;
                while (inputIndex < 0) {
                    inputIndex = codec.dequeueInputBuffer(10000);
                    if (inputIndex >= 0) {
                        ByteBuffer input = codec.getInputBuffer(inputIndex);
                        input.clear();
                        byte[] yuv = new byte[width * height * 3 / 2];
                        for (int y = 0; y < height; y++) {
                            for (int x = 0; x < width; x++) {
                                yuv[y * width + x] = (byte) ((x + y + frame * 8) & 0xFF);
                            }
                        }
                        java.util.Arrays.fill(yuv, width * height, yuv.length, (byte) 128);
                        input.put(yuv);
                        codec.queueInputBuffer(inputIndex, 0, yuv.length,
                                (long) frame * 1000000L / frameRate, 0);
                    }
                }
                while (true) {
                    int outputIndex = codec.dequeueOutputBuffer(info, 0);
                    if (outputIndex >= 0) {
                        if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                            configBytes += info.size;
                            MediaFormat outputFormat = codec.getOutputFormat();
                            Log.i(TAG, "AVC encoder config=" + info.size
                                    + " csd=" + outputFormat.containsKey("csd-0"));
                        } else {
                            encodedBytes += info.size;
                        }
                        codec.releaseOutputBuffer(outputIndex, false);
                    } else {
                        break;
                    }
                }
            }
            boolean inputDone = false;
            while (!inputDone) {
                int inputIndex = codec.dequeueInputBuffer(10000);
                if (inputIndex >= 0) {
                    codec.queueInputBuffer(inputIndex, 0, 0, 10L * 1000000L / frameRate,
                            MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                }
            }
            while (true) {
                int outputIndex = codec.dequeueOutputBuffer(info, 10000);
                if (outputIndex >= 0) {
                    if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                        configBytes += info.size;
                    } else {
                        encodedBytes += info.size;
                    }
                    codec.releaseOutputBuffer(outputIndex, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        break;
                    }
                }
            }
            Log.i(TAG, "MediaCodec encoded video: " + encodedBytes
                    + " bytes config=" + configBytes);
        } catch (Throwable t) {
            Log.e(TAG, "video encoder failed", t);
        } finally {
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Throwable ignored) {
                }
                codec.release();
            }
        }
    }

    private void startCodec(final Surface surface) {
        if (mCodecThread != null) {
            return;
        }
        mCodecThread = new Thread(new Runnable() {
            @Override
            public void run() {
                testCodec(surface);
                testAudioCodec();
                testEncoder();
            }
        }, "media-codec-test");
        mCodecThread.start();
    }

    private void testCodec(final Surface surface) {
        MediaExtractor extractor = new MediaExtractor();
        MediaCodec codec = null;
        try (AssetFileDescriptor afd = getAssets().openFd("sample.mp4")) {
            extractor.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(),
                    afd.getDeclaredLength());
            int videoTrack = -1;
            MediaFormat format = null;
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat candidate = extractor.getTrackFormat(i);
                String mime = candidate.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("video/")) {
                    videoTrack = i;
                    format = candidate;
                    break;
                }
            }
            if (videoTrack < 0) {
                Log.w(TAG, "no video track");
                return;
            }
            extractor.selectTrack(videoTrack);
            final MediaExtractor mediaExtractor = extractor;
            final CountDownLatch done = new CountDownLatch(1);
            final boolean[] inputDone = {false};
            final String[] error = {null};
            String codecName = new MediaCodecList(MediaCodecList.REGULAR_CODECS)
                    .findDecoderForFormat(format);
            Log.i(TAG, "video decoder: " + codecName);
            codec = codecName != null
                    ? MediaCodec.createByCodecName(codecName)
                    : MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME));
            Log.i(TAG, "video codec info: " + codec.getCodecInfo().getName());
            codec.setCallback(new MediaCodec.Callback() {
                @Override
                public void onInputBufferAvailable(MediaCodec mc, int index) {
                    if (inputDone[0]) {
                        return;
                    }
                    ByteBuffer input = mc.getInputBuffer(index);
                    boolean advanced = mediaExtractor.advance();
                    int size = advanced ? mediaExtractor.readSampleData(input, 0) : -1;
                    if (size < 0) {
                        mc.queueInputBuffer(index, 0, 0, 0,
                                MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        inputDone[0] = true;
                        return;
                    }
                    mc.queueInputBuffer(index, 0, size, mediaExtractor.getSampleTime(), 0);
                }

                @Override
                public void onOutputBufferAvailable(MediaCodec mc, int index,
                                                    MediaCodec.BufferInfo info) {
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        mc.releaseOutputBuffer(index, false);
                        Log.i(TAG, "MediaCodec async reached end of stream");
                        done.countDown();
                        return;
                    }
                    mc.releaseOutputBuffer(index, true);
                }

                @Override
                public void onError(MediaCodec mc, MediaCodec.CodecException e) {
                    error[0] = "async codec error: " + e;
                    Log.e(TAG, error[0], e);
                    done.countDown();
                }

                @Override
                public void onOutputFormatChanged(MediaCodec mc, MediaFormat outputFormat) {
                    Log.i(TAG, "MediaCodec output format: " + outputFormat);
                }
            });
            codec.configure(format, surface, null, 0);
            codec.start();
            if (!done.await(15, TimeUnit.SECONDS)) {
                throw new IllegalStateException("async codec timed out");
            }
            if (error[0] != null) {
                throw new IllegalStateException(error[0]);
            }
            mStatus.post(new Runnable() {
                @Override
                public void run() {
                    mStatus.setText("MediaCodec async decoded video");
                }
            });
            Log.i(TAG, "MediaCodec async decoded video");
        } catch (Throwable t) {
            Log.e(TAG, "MediaCodec failed", t);
            final String message = "MediaCodec failed: " + t;
            mStatus.post(new Runnable() {
                @Override
                public void run() {
                    mStatus.setText(message);
                }
            });
        } finally {
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Throwable ignored) {
                }
                codec.release();
            }
            extractor.release();
        }
    }

    private void testAudioCodec() {
        MediaExtractor extractor = new MediaExtractor();
        MediaCodec codec = null;
        AudioTrack track = null;
        try (AssetFileDescriptor afd = getAssets().openFd("sample.mp4")) {
            extractor.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(),
                    afd.getDeclaredLength());
            int audioTrackIndex = -1;
            MediaFormat format = null;
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat candidate = extractor.getTrackFormat(i);
                String mime = candidate.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) {
                    audioTrackIndex = i;
                    format = candidate;
                    break;
                }
            }
            if (audioTrackIndex < 0) {
                Log.w(TAG, "no audio track");
                return;
            }
            extractor.selectTrack(audioTrackIndex);
            int sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE);
            int channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            int channelMask = channels == 1
                    ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
            int minBuffer = AudioTrack.getMinBufferSize(sampleRate, channelMask,
                    AudioFormat.ENCODING_PCM_16BIT);
            track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                            .build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(sampleRate)
                            .setChannelMask(channelMask)
                            .build())
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(Math.max(minBuffer, sampleRate))
                    .build();
            if (track.getState() != AudioTrack.STATE_INITIALIZED) {
                throw new IllegalStateException("AudioTrack state=" + track.getState());
            }
            track.play();
            String codecName = new MediaCodecList(MediaCodecList.REGULAR_CODECS)
                    .findDecoderForFormat(format);
            Log.i(TAG, "audio decoder: " + codecName);
            codec = codecName != null
                    ? MediaCodec.createByCodecName(codecName)
                    : MediaCodec.createDecoderByType(format.getString(MediaFormat.KEY_MIME));
            Log.i(TAG, "audio codec info: " + codec.getCodecInfo().getName());
            codec.configure(format, null, null, 0);
            codec.start();
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inputDone = false;
            long totalBytes = 0;
            while (true) {
                if (!inputDone) {
                    int inputIndex = codec.dequeueInputBuffer(10000);
                    if (inputIndex >= 0) {
                        ByteBuffer input = codec.getInputBuffer(inputIndex);
                        boolean advanced = extractor.advance();
                        int size = advanced ? extractor.readSampleData(input, 0) : -1;
                        if (size < 0) {
                            codec.queueInputBuffer(inputIndex, 0, 0, 0,
                                    MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            codec.queueInputBuffer(inputIndex, 0, size,
                                    extractor.getSampleTime(), 0);
                        }
                    }
                }
                int outputIndex = codec.dequeueOutputBuffer(info, 10000);
                if (outputIndex >= 0) {
                    if (info.size > 0) {
                        ByteBuffer output = codec.getOutputBuffer(outputIndex);
                        byte[] pcm = new byte[info.size];
                        output.position(info.offset);
                        output.limit(info.offset + info.size);
                        output.get(pcm);
                        track.write(pcm, 0, pcm.length);
                        totalBytes += pcm.length;
                    }
                    codec.releaseOutputBuffer(outputIndex, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        break;
                    }
                }
            }
            Log.i(TAG, "MediaCodec decoded audio: " + totalBytes + " bytes at "
                    + sampleRate + "Hz " + channels + "ch");
            mStatus.post(new Runnable() {
                @Override
                public void run() {
                    mStatus.setText("MediaCodec decoded audio");
                }
            });
        } catch (Throwable t) {
            Log.e(TAG, "MediaCodec audio failed", t);
        } finally {
            if (track != null) {
                track.stop();
                track.release();
            }
            if (codec != null) {
                try {
                    codec.stop();
                } catch (Throwable ignored) {
                }
                codec.release();
            }
            extractor.release();
        }
    }

    private void stopTrack() {
        if (mTrack != null) {
            try {
                mTrack.stop();
            } catch (IllegalStateException ignored) {
            }
            mTrack.release();
            mTrack = null;
        }
    }

    private void testExtractor() {
        try (AssetFileDescriptor afd = getAssets().openFd("sample.mp4")) {
            MediaExtractor extractor = new MediaExtractor();
            extractor.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(),
                    afd.getDeclaredLength());
            StringBuilder text = new StringBuilder();
            text.append("Extractor: ").append(extractor.getTrackCount()).append(" tracks");
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat format = extractor.getTrackFormat(i);
                text.append("\n[").append(i).append("] ")
                        .append(format.getString(MediaFormat.KEY_MIME));
                if (format.containsKey(MediaFormat.KEY_WIDTH)) {
                    text.append(" ").append(format.getInteger(MediaFormat.KEY_WIDTH))
                            .append("x").append(format.getInteger(MediaFormat.KEY_HEIGHT));
                }
                if (format.containsKey(MediaFormat.KEY_SAMPLE_RATE)) {
                    text.append(" ").append(format.getInteger(MediaFormat.KEY_SAMPLE_RATE))
                            .append("Hz");
                }
            }
            int videoTrack = -1;
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                String mime = extractor.getTrackFormat(i).getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("video/")) {
                    videoTrack = i;
                    break;
                }
            }
            MediaCodecList codecList = new MediaCodecList(MediaCodecList.REGULAR_CODECS);
            MediaCodecInfo[] codecInfos = codecList.getCodecInfos();
            text.append("\ncodecs: ").append(codecInfos.length);
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat trackFormat = extractor.getTrackFormat(i);
                text.append("\ndecoder[").append(i).append("]: ")
                        .append(codecList.findDecoderForFormat(trackFormat));
            }
            if (videoTrack >= 0) {
                extractor.selectTrack(videoTrack);
                boolean advanced = extractor.advance();
                ByteBuffer buffer = ByteBuffer.allocateDirect(1024 * 1024);
                int size = advanced ? extractor.readSampleData(buffer, 0) : -1;
                text.append("\nsample: ").append(size).append(" bytes, time=")
                        .append(extractor.getSampleTime()).append("us, flags=0x")
                        .append(Integer.toHexString(extractor.getSampleFlags()));
            }
            extractor.release();
            mExtractor.setText(text);
            Log.i(TAG, text.toString());
        } catch (Throwable t) {
            mExtractor.setText("Extractor failed: " + t);
            Log.e(TAG, "Extractor failed", t);
        }
    }

    @Override
    protected void onDestroy() {
        stopTrack();
        super.onDestroy();
    }
}
