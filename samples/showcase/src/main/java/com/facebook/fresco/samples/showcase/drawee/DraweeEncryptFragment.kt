package com.facebook.fresco.samples.showcase.drawee

import android.content.Context
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Bundle
import android.os.Environment
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
import kotlinx.coroutines.*
import org.json.JSONArray
import org.json.JSONObject

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.net.URL
import java.util.*

class DraweeEncryptFragment : BaseShowcaseFragment() {

    private val TAG = "DraweeEncryptFragment"

    private var mDraweeEncryptView: SimpleDraweeView? = null
    private var mDraweeDecryptView: SimpleDraweeView? = null
    private var mDraweeDecryptDiskView: SimpleDraweeView? = null
    private var mUri: Uri? = null

    private var encryptedImageDir: File? = null
    private var downloadDir: File? = null
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

        //encryptedImageDir = Preconditions.checkNotNull<Context>(context).getExternalFilesDir(null)
        encryptedImageDir = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "pdk-showcase")
        downloadDir = File(encryptedImageDir, "downloads")
        downloadDir!!.mkdirs()
        FLog.d(TAG, "downloadDir=$downloadDir, exists=${downloadDir!!.exists()}")

        mUri = sampleUris().createSampleUri()
        mDraweeEncryptView = view.findViewById(R.id.drawee_view)
        mDraweeDecryptView = view.findViewById(R.id.drawee_decrypt)
        mDraweeDecryptDiskView = view.findViewById(R.id.drawee_decrypt_disk)
        setNewKey()

        setEncryptOptions()

        view.findViewById<View>(R.id.btn_random_uri)
                .setOnClickListener {
                    mUri = sampleUris().createSampleUri()
                    //mUri = Uri.parse("")
                    setNewKey()
                    setEncryptOptions()
                }

        view.findViewById<View>(R.id.btn_decrypt_image)
                .setOnClickListener { setDecryptOptions() }

        view.findViewById<View>(R.id.btn_decrypt_image_disk)
                .setOnClickListener { setDecryptFromUrlOptions() }

        view.findViewById<View>(R.id.btn_start_ml_labeling).setOnClickListener {
            val dlDir = downloadDir!!
            val imageList = dlDir.listFiles { _, name: String? ->
                name?.toLowerCase(Locale.US)?.endsWith(".jpg") ?: false
            }

            labelFiles(imageList.toList())
        }

        view.findViewById<View>(R.id.btn_start_batch_encrypt).setOnClickListener {
            downloadImagesFromRemoteList {
                encryptFiles(it)
            }
        }

        view.findViewById<View>(R.id.btn_start_batch_decrypt).setOnClickListener {
            decryptFiles(downloadDir!!.listFiles().toList())
        }
    }

    private fun setNewKey(useStaticKey: Boolean = true) {
        lastKey = if (useStaticKey) {
            JpegCryptoKey.Builder()
                    .setX0("0.776129673739571545164782701951000816488709002838321334050408728659596467124659438412371823627863280e-1")
                    .setMu("3.669367621207023984299275643031978184898674105584480292875745487930315472819066461859774116271133194e0")
                    .build()
        } else {
            JpegCryptoKey.Builder().generateNewValues(72, 72).build()
        }
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
        setNewKey(true)
        mUri = Uri.parse("http://r/325360954_a43b5b2b99_o_encrypted.jpg")
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

    @Synchronized
    private fun downloadImagesFromRemoteList(onDownloadcomplete: (List<File>) -> Unit) {
        if (!downloadDir!!.mkdir() && !downloadDir!!.exists()) {
            throw RuntimeException("Failed to create download dir for images: " + downloadDir!!.absolutePath)
        }

        val downloader = TestImageDownloader(downloadDir!!)

        val listUrl = URL(getString(R.string.image_list_url))

        FLog.d(TAG, "Got listUrl=$listUrl")

        downloader.downloadFromList(listUrl) { files ->
            onDownloadcomplete(files)
        }
    }

    private fun labelFiles(files: List<File>) {
        val jsonObjs = Collections.synchronizedList(mutableListOf<JSONObject>())
        val analyzer = MLKitAnalyzer(Preconditions.checkNotNull<Context>(this.context))

        GlobalScope.launch(Dispatchers.Main) {
            // Process only one image at a time
            for (imageFile in files) {
                withContext(Dispatchers.IO) {
                    scanFile(imageFile.absolutePath)
                    FLog.d(TAG, "Invoking analyze")
                    analyzer.analyze(Uri.fromFile(imageFile), MLKitAnalyzer.Labeler.ON_DEVICE) { labels ->
                        jsonObjs.add(analyzer.toJson(Uri.fromFile(imageFile).lastPathSegment!!, labels))

                        if (files.size == jsonObjs.size) {
                            FLog.d(TAG, "Final json objs: $jsonObjs")
                            val outputFilename = File(encryptedImageDir, "downloads")
                                    .resolve("${System.currentTimeMillis()}_${getString(R.string.ml_results_file)}")

                            val jsonArray = JSONArray(jsonObjs).toString(2)
                            FLog.d(TAG, "Final jsonArray: $jsonArray")

                            outputFilename.writeText(jsonArray)
                            FLog.d(TAG, "Wrote ML labels to ${outputFilename.absolutePath}")
                            scanFile(outputFilename.absolutePath)
                        }
                    }
                }
            }
        }
    }

    private fun encryptImage(image: File, callback: (encryptedFile: File) -> Unit) {
        val outputFile = image.parentFile.resolve("${image.nameWithoutExtension}_encrypted.${image.extension}")

        if (outputFile.exists()) {
            FLog.d(TAG, "Encrypted image ${outputFile.name} already exists, skipping encryption")
            return
        }

        pipeline!!.clearCaches()
        setNewKey(true)
        val fileUri = Uri.fromFile(image)
        val imageRequest = ImageRequestBuilder.newBuilderWithSource(fileUri)
                .setEncrypt(true)
                .setJpegCryptoKey(lastKey)
                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                .build()

        val dataSource = pipeline!!.fetchEncodedImage(imageRequest, this)

        val factory = NativeImageEncryptorFactory.getNativeImageEncryptorFactory()


        FLog.d(TAG, "encryptImage() writing to output file: $outputFile")
        dataSourceToDisk(dataSource, null, factory, null, outputFile, callback)
    }

    private fun encryptFiles(files: List<File>) {
        GlobalScope.launch(Dispatchers.Main) {
            // Process only one image at a time
            for (imageFile in files) {
                scanFile(imageFile.absolutePath)
                if (imageFile.nameWithoutExtension.contains("encrypted")) {
                    continue
                }
                withContext(Dispatchers.IO) {
                    FLog.d(TAG, "Encrypting image $imageFile")
                    encryptImage(imageFile) {
                        val fileSizeCsvRow = "CSV: ${imageFile.name},${imageFile.length()},${it.length()},${it.length() * 1.0 / imageFile.length()}"
                        FLog.d(TAG, fileSizeCsvRow)
                    }
                }
            }
        }
    }

    private fun decryptImage(image: File, callback: (encryptedFile: File) -> Unit) {
        val outputFile = image.parentFile.resolve("${image.nameWithoutExtension.replace("encrypted", "decrypted")}.${image.extension}")

        if (outputFile.exists()) {
            FLog.d(TAG, "Decrypted image ${outputFile.name} already exists, skipping decryption")
            return
        }

        pipeline!!.clearCaches()
        setNewKey(true)
        val fileUri = Uri.fromFile(image)
        val imageRequest = ImageRequestBuilder.newBuilderWithSource(fileUri)
                .setDecrypt(true)
                .setJpegCryptoKey(lastKey)
                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                .build()

        val dataSource = pipeline!!.fetchEncodedImage(imageRequest, this)

        val factory = NativeImageDecryptorFactory.getNativeImageDecryptorFactory()

        FLog.d(TAG, "decryptImage() writing to output file: $outputFile")
        dataSourceToDisk(dataSource, null, null, factory, outputFile, callback)
    }

    private fun decryptFiles(files: List<File>) {
        GlobalScope.launch(Dispatchers.Main) {
            // Process only one image at a time
            for (imageFile in files) {
                if (!imageFile.nameWithoutExtension.contains("encrypted")) {
                    continue
                }
                withContext(Dispatchers.IO) {
                    FLog.d(TAG, "Decrypting image $imageFile")
                    decryptImage(imageFile) {
                        FLog.d(TAG, "Finished decrypting image $imageFile")
                    }
                }
            }
        }
    }

    private fun scanFile(path: String) {
        MediaScannerConnection.scanFile(context!!.applicationContext, arrayOf(path), null, msClient)
    }

    private fun dataSourceToDisk(dataSource: DataSource<CloseableReference<PooledByteBuffer>>,
                                 viewToDisplayWith: SimpleDraweeView?,
                                 encryptorFactory: ImageEncryptorFactory?,
                                 decryptorFactory: ImageDecryptorFactory?,
                                 outputFile: File? = null,
                                 callback: ((encryptedFile: File) -> Unit)? = null) {
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
                    var outputImageFile: File? = null
                    try {
                        val encodedImage = EncodedImage(ref)
                        val `is` = encodedImage.inputStream

                        try {
                            outputImageFile = outputFile
                                    ?: File.createTempFile(mUri!!.lastPathSegment, ".jpg", encryptedImageDir)

                            val fileOutputStream = FileOutputStream(outputImageFile)

                            if (encryptorFactory != null) {
                                val encryptor = encryptorFactory.createImageEncryptor(encodedImage.imageFormat)
                                encryptor.encrypt(encodedImage, fileOutputStream, lastKey)
                                FLog.d(TAG, "Wrote %s encrypted to %s (size: %s bytes)", mUri, outputImageFile!!.absolutePath, outputImageFile.length() / 8)
                            } else if (decryptorFactory != null) {
                                val decryptor = decryptorFactory.createImageDecryptor(encodedImage.imageFormat)
                                decryptor.decrypt(encodedImage, fileOutputStream, lastKey)
                                FLog.d(TAG, "Wrote %s decrypted to %s (size: %s bytes)", mUri, outputImageFile!!.absolutePath, outputImageFile.length() / 8)
                            }

                            scanFile(outputImageFile!!.absolutePath)

                            Closeables.close(fileOutputStream, true)
                        } catch (e: IOException) {
                            FLog.e(TAG, "IOException while trying to write encrypted JPEG to disk", e)
                        } finally {
                            Closeables.closeQuietly(`is`)
                        }
                    } finally {
                        CloseableReference.closeSafely(ref)
                    }

                    if (outputImageFile != null) {
                        lastSavedImage = Uri.parse(outputImageFile.toURI().toString())

                        if (viewToDisplayWith != null) {
                            val encryptedImageRequest = ImageRequestBuilder.newBuilderWithSource(lastSavedImage)
                                    .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                                    .build()
                            viewToDisplayWith.setImageRequest(encryptedImageRequest)
                        }

                        if (callback != null) {
                            callback(outputImageFile)
                        }
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
