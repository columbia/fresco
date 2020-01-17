package com.facebook.fresco.samples.showcase.drawee

import android.content.Context
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup

import com.facebook.common.internal.Closeables
import com.facebook.common.internal.Preconditions
import com.facebook.common.logging.FLog
import com.facebook.common.memory.PooledByteBuffer
import com.facebook.common.references.CloseableReference
import com.facebook.datasource.BaseDataSubscriber
import com.facebook.datasource.DataSource
import com.facebook.drawee.backends.pipeline.Fresco
import com.facebook.drawee.view.SimpleDraweeView
import com.facebook.fresco.samples.showcase.BaseShowcaseFragment
import com.facebook.fresco.samples.showcase.R
import com.facebook.fresco.samples.showcase.misc.MLKitAnalyzer
import com.facebook.fresco.samples.showcase.misc.TestImageDownloader
import com.facebook.imagepipeline.common.ImageDecodeOptionsBuilder
import com.facebook.imagepipeline.common.JpegCryptoKey
import com.facebook.imagepipeline.core.DefaultExecutorSupplier
import com.facebook.imagepipeline.core.ImagePipeline
import com.facebook.imagepipeline.decryptor.ImageDecryptorFactory
import com.facebook.imagepipeline.encryptor.ImageEncryptorFactory
import com.facebook.imagepipeline.image.EncodedImage
import com.facebook.imagepipeline.nativecode.NativeImageDecryptorFactory
import com.facebook.imagepipeline.nativecode.NativeImageEncryptorFactory
import com.facebook.imagepipeline.request.ImageRequestBuilder

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.URL

class DraweeEncryptFragment : BaseShowcaseFragment() {

    private val TAG = "DraweeEncryptFragment"

    private var mDraweeEncryptView: SimpleDraweeView? = null
    private var mDraweeDecryptView: SimpleDraweeView? = null
    private var mDraweeDecryptDiskView: SimpleDraweeView? = null
    private var mUri: Uri? = null

    private var encryptedImageDir: File? = null
    private var lastSavedImage: Uri? = null
    private var lastKey: JpegCryptoKey? = null

    private var pipeline: ImagePipeline? = null

    private val msClient = object : MediaScannerConnection.MediaScannerConnectionClient {
        override fun onMediaScannerConnected() {
            FLog.d(TAG, "MediaScanner connected")
        }

        override fun onScanCompleted(path: String, uri: Uri) {
            FLog.d(TAG, "Done scanning %s", path)
        }
    }

    override fun onCreateView(
            inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        return inflater.inflate(R.layout.fragment_drawee_encrypt, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        pipeline = Fresco.getImagePipeline()

        encryptedImageDir = Preconditions.checkNotNull<Context>(context).getExternalFilesDir(null)

        mUri = sampleUris().createSampleUri()
        mDraweeEncryptView = view.findViewById(R.id.drawee_view)
        mDraweeDecryptView = view.findViewById(R.id.drawee_decrypt)
        mDraweeDecryptDiskView = view.findViewById(R.id.drawee_decrypt_disk)
        setNewKey()

        setEncryptOptions()

        view.findViewById<View>(R.id.btn_random_uri)
                .setOnClickListener {
                    mUri = sampleUris().createSampleUri()
                    mUri = Uri.parse("")
                    setNewKey()
                    setEncryptOptions()
                }

        view.findViewById<View>(R.id.btn_decrypt_image)
                .setOnClickListener { setDecryptOptions() }

        view.findViewById<View>(R.id.btn_decrypt_image_disk)
                .setOnClickListener { setDecryptFromUrlOptions() }

        view.findViewById<View>(R.id.btn_start_ml_labeling)
                .setOnClickListener { labelImagesFromRemoteList() }
    }

    private fun setNewKey() {
        lastKey = JpegCryptoKey.Builder().generateNewValues(72, 72).build()
        lastKey = JpegCryptoKey.Builder()
                .setX0("0.776129673739571545164782701951000816488709002838321334050408728659596467124659438412371823627863280e-1")
                .setMu("3.669367621207023984299275643031978184898674105584480292875745487930315472819066461859774116271133194e0")
                .build()
        FLog.d(TAG, "Set lastKey: %s", lastKey)
    }

    private fun setEncryptOptions() {
        pipeline!!.clearCaches()
        val imageRequest = ImageRequestBuilder.newBuilderWithSource(mUri)
                .setEncrypt(true)
                .setJpegCryptoKey(lastKey)
                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                .build()

        val dataSource = pipeline!!
                .fetchEncodedImage(imageRequest, this)

        val factory = NativeImageEncryptorFactory.getNativeImageEncryptorFactory()

        dataSourceToDisk(dataSource, mDraweeEncryptView, factory, null)
    }

    private fun setDecryptOptions() {
        if (lastSavedImage != null) {
            pipeline!!.clearCaches()
            val imageRequest = ImageRequestBuilder.newBuilderWithSource(lastSavedImage)
                    .setDecrypt(true)
                    .setJpegCryptoKey(lastKey)
                    .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                    .build()
            val dataSource = pipeline!!
                    .fetchEncodedImage(imageRequest, this)

            val factory = NativeImageDecryptorFactory.getNativeImageDecryptorFactory()

            dataSourceToDisk(dataSource, mDraweeDecryptView, null, factory)
        }
    }

    private fun setDecryptFromUrlOptions() {
        pipeline!!.clearCaches()
        mUri = Uri.parse("")
        val imageRequest = ImageRequestBuilder.newBuilderWithSource(mUri)
                .setDecrypt(true)
                .setJpegCryptoKey(lastKey)
                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                .build()
        FLog.d(TAG, "Decrypting from url %s", mUri!!.toString())
        val dataSource = pipeline!!
                .fetchEncodedImage(imageRequest, this)

        val factory = NativeImageDecryptorFactory.getNativeImageDecryptorFactory()

        dataSourceToDisk(dataSource, mDraweeDecryptDiskView, null, factory)
        //mDraweeDecryptDiskView.setImageRequest(imageRequest);
    }

    private fun labelImagesFromRemoteList() {
        val downloadDir = File(encryptedImageDir, "downloads")

        if (!downloadDir.mkdir() && !downloadDir.exists()) {
            throw RuntimeException("Failed to create download dir for images: " + downloadDir.absolutePath)
        }

        val downloader = TestImageDownloader(downloadDir)

        val listUrl = URL(getString(R.string.image_list_url))

        FLog.d(TAG, "Got listUrl=$listUrl")

        downloader.downloadFromList(listUrl) {
            for (imageFile in it) {
                scanFile(imageFile.absolutePath)
            }
            val analyzer = MLKitAnalyzer(Preconditions.checkNotNull<Context>(this.context))

            //analyzer.analyze(filePath, MLKitAnalyzer.Labeler.ON_DEVICE);
        }
    }

    private fun scanFile(path: String) {
        MediaScannerConnection.scanFile(context, arrayOf(path), null, msClient)
    }

    private fun dataSourceToDisk(dataSource: DataSource<CloseableReference<PooledByteBuffer>>,
                                 viewToDisplayWith: SimpleDraweeView?,
                                 encryptorFactory: ImageEncryptorFactory?,
                                 decryptorFactory: ImageDecryptorFactory?) {
        val executor = DefaultExecutorSupplier(1).forBackgroundTasks()

        val dataSubscriber = object : BaseDataSubscriber<CloseableReference<PooledByteBuffer>>() {
            override fun onNewResultImpl(
                    dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                if (!dataSource.isFinished) {
                    // if we are not interested in the intermediate images,
                    // we can just return here.
                    return
                }

                val ref = dataSource.result
                if (ref != null) {
                    var tempFile: File? = null
                    try {
                        val encodedImage = EncodedImage(ref)
                        val `is` = encodedImage.inputStream

                        try {
                            tempFile = File.createTempFile(mUri!!.lastPathSegment, ".jpg", encryptedImageDir)
                            val fileOutputStream = FileOutputStream(tempFile)

                            if (encryptorFactory != null) {
                                val encryptor = encryptorFactory.createImageEncryptor(encodedImage.imageFormat)
                                encryptor.encrypt(encodedImage, fileOutputStream, lastKey)
                                FLog.d(TAG, "Wrote %s encrypted to %s (size: %s bytes)", mUri, tempFile!!.absolutePath, tempFile.length() / 8)
                            } else if (decryptorFactory != null) {
                                val decryptor = decryptorFactory.createImageDecryptor(encodedImage.imageFormat)
                                decryptor.decrypt(encodedImage, fileOutputStream, lastKey)
                                FLog.d(TAG, "Wrote %s decrypted to %s (size: %s bytes)", mUri, tempFile!!.absolutePath, tempFile.length() / 8)
                            }

                            scanFile(tempFile!!.absolutePath)

                            Closeables.close(fileOutputStream, true)
                        } catch (e: IOException) {
                            FLog.e(TAG, "IOException while trying to write encrypted JPEG to disk", e)
                        } finally {
                            Closeables.closeQuietly(`is`)
                        }
                    } finally {
                        CloseableReference.closeSafely(ref)
                    }

                    if (tempFile != null) {
                        lastSavedImage = Uri.parse(tempFile.toURI().toString())
                        val encryptedImageRequest = ImageRequestBuilder.newBuilderWithSource(lastSavedImage)
                                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                                .build()
                        viewToDisplayWith?.setImageRequest(encryptedImageRequest)
                    }
                }
            }

            override fun onFailureImpl(dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                val t = dataSource.failureCause
                FLog.e(TAG, "Failed to load and encrypt/decrypt JPEG", t)
            }
        }

        dataSource.subscribe(dataSubscriber, executor)
    }

    override fun getTitleId(): Int {
        return R.string.drawee_encrypt_title
    }
}
