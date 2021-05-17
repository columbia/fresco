package com.facebook.fresco.samples.showcase.drawee

import android.content.Context
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.util.Log
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
import com.facebook.imagepipeline.common.ImageDecodeOptions
import com.facebook.imagepipeline.common.ImageDecodeOptionsBuilder
import com.facebook.imagepipeline.common.JpegCryptoKey
import com.facebook.imagepipeline.common.ResizeOptions
import com.facebook.imagepipeline.core.DefaultExecutorSupplier
import com.facebook.imagepipeline.core.ImagePipeline
import com.facebook.imagepipeline.decryptor.ImageDecryptorFactory
import com.facebook.imagepipeline.encryptor.ImageEncryptorFactory
import com.facebook.imagepipeline.image.EncodedImage
import com.facebook.imagepipeline.nativecode.NativeImageDecryptorFactory
import com.facebook.imagepipeline.nativecode.NativeImageEncryptorFactory
import com.facebook.imagepipeline.nativecode.NativeImageTranscoderFactory
import com.facebook.imagepipeline.request.ImageRequest
import com.facebook.imagepipeline.request.ImageRequestBuilder
import kotlinx.android.synthetic.main.fragment_vito_view_ktx.*
import kotlinx.coroutines.*
import org.json.JSONArray
import org.json.JSONObject

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.OutputStream
import java.lang.IllegalArgumentException
import java.net.URL
import java.util.*
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine
import kotlin.system.measureTimeMillis

class DraweeEncryptFragment : BaseShowcaseFragment() {

    private val TAG = "DraweeEncryptFragment"

    private val JPEG_QUALITY = 50

    private var mDraweeEncryptView: SimpleDraweeView? = null
    private var mDraweeDecryptView: SimpleDraweeView? = null
    private var mDraweeDecryptDiskView: SimpleDraweeView? = null
    private var mUri: Uri? = null

    private var encryptedImageDir: File? = null
    private var downloadOriginalsDir: File? = null
    private var encryptedOutputDir: File? = null
    private var encryptedGooglePhotosCompressedDir: File? = null
    private var thumbnailDir: File? = null
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

        downloadOriginalsDir = File(encryptedImageDir, "originals")
        downloadOriginalsDir!!.mkdirs()
        FLog.d(TAG, "downloadOriginalsDir=$downloadOriginalsDir, exists=${downloadOriginalsDir!!.exists()}")

        encryptedOutputDir = File(encryptedImageDir, "encrypted-output")
        encryptedOutputDir!!.mkdirs()
        FLog.d(TAG, "encryptedOutputDir=$encryptedOutputDir, exists=${encryptedOutputDir!!.exists()}")

        encryptedGooglePhotosCompressedDir = File(encryptedImageDir, "encrypted-gp-compressed")
        encryptedGooglePhotosCompressedDir!!.mkdirs()
        FLog.d(TAG, "encryptedGooglePhotosCompressedDir=$encryptedGooglePhotosCompressedDir, exists=${encryptedGooglePhotosCompressedDir!!.exists()}")

        // Make dir to hold thumbnails
        thumbnailDir = File(encryptedImageDir, "resized-thumbnails")
        thumbnailDir!!.mkdirs()
        FLog.d(TAG, "tempThumbnailDir=$thumbnailDir, exists=${thumbnailDir!!.exists()}")

        mUri = sampleUris().createSampleUri()
        mDraweeEncryptView = view.findViewById(R.id.drawee_view)
        mDraweeDecryptView = view.findViewById(R.id.drawee_decrypt)
        mDraweeDecryptDiskView = view.findViewById(R.id.drawee_decrypt_disk)
        setNewKey()

        //setEncryptOptions()

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
            val dlDir = encryptedOutputDir!!
            val imageList = dlDir.listFiles { _, name: String? ->
                name?.toLowerCase(Locale.US)?.endsWith(".jpg") ?: false
            }

            labelFiles(imageList.toList())
        }

        view.findViewById<View>(R.id.btn_start_batch_encrypt).setOnClickListener {
            downloadImagesFromRemoteList {
                //encryptFiles(it)
                encryptEtcFiles(it)
            }
        }

        view.findViewById<View>(R.id.btn_start_batch_encrypt_thumbnails).setOnClickListener {
            makeThumbnails(downloadOriginalsDir!!.listFiles().toList().filter { it.extension.endsWith("jpg") }) {
                encryptEtcFiles(it)
            }
        }

        view.findViewById<View>(R.id.btn_start_batch_decrypt).setOnClickListener {
            //decryptFiles(downloadDir!!.listFiles().toList())
            decryptEtcFiles(encryptedOutputDir!!.listFiles().toList())
        }

        view.findViewById<View>(R.id.btn_start_batch_decrypt_gp_compressed).setOnClickListener {
            decryptEtcFiles(encryptedGooglePhotosCompressedDir!!.listFiles().toList())
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

        encryptDataSourceToDisk(dataSource, mDraweeEncryptView, factory, null)
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

            decryptDataSourceToDisk(dataSource, null, null, mDraweeDecryptView, factory)
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

        decryptDataSourceToDisk(dataSource, null, null, mDraweeDecryptDiskView, factory)
        //mDraweeDecryptDiskView.setImageRequest(imageRequest);
    }

    @Synchronized
    private fun downloadImagesFromRemoteList(onDownloadcomplete: (List<File>) -> Unit) {
        if (!downloadOriginalsDir!!.mkdir() && !downloadOriginalsDir!!.exists()) {
            throw RuntimeException("Failed to create download dir for images: " + downloadOriginalsDir!!.absolutePath)
        }

        val downloader = TestImageDownloader(downloadOriginalsDir!!)

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
        encryptDataSourceToDisk(dataSource, null, factory, outputFile, null, null, callback)
    }

    private fun encryptEtcImage(image: File, callback: (encryptedFile: File?) -> Unit) {
        val outputFileRed = encryptedOutputDir!!.resolve("${image.nameWithoutExtension}_encrypted_red.${image.extension}")
        val outputFileGreen = encryptedOutputDir!!.resolve("${image.nameWithoutExtension}_encrypted_green.${image.extension}")
        val outputFileBlue = encryptedOutputDir!!.resolve("${image.nameWithoutExtension}_encrypted_blue.${image.extension}")

        if (outputFileRed.exists() && outputFileRed.length() > 0) {
            FLog.d(TAG, "Encrypted image ${outputFileRed.name} already exists, skipping encryption")
            callback(null)
            return
        }

        pipeline!!.clearCaches()
        setNewKey(true)
        val fileUri = Uri.fromFile(image)
        val imageRequest = ImageRequestBuilder.newBuilderWithSource(fileUri)
                .setEncryptEtc(true)
                .setJpegCryptoKey(lastKey)
                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                .build()

        val dataSource = pipeline!!.fetchEncodedImage(imageRequest, this)

        val factory = NativeImageEncryptorFactory.getNativeImageEncryptorFactory()

        FLog.d(TAG, "encryptEtcImage() writing to output file: $outputFileRed / $outputFileGreen / $outputFileBlue")
        encryptDataSourceToDisk(dataSource, null, factory,
                outputFileRed,
                outputFileGreen,
                outputFileBlue,
                callback)
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

    private fun makeThumbnails(originalFiles: List<File>, onThumbnailsComplete: (List<File>) -> Unit) {
        val resizeOptions = ResizeOptions(400, 400)
        val quality = 50

        GlobalScope.launch(Dispatchers.IO) {
            for (imageFile in originalFiles) {
                val imageUri = Uri.fromFile(imageFile)
                val imageRequest = ImageRequestBuilder.newBuilderWithSource(imageUri)
                        .setImageDecodeOptions(ImageDecodeOptions.defaults())
                        .build()

                val tempFile = File(thumbnailDir, "${imageFile.nameWithoutExtension}_thumbnail.jpg")
                tempFile.createNewFile()
                val intermediateResizeOutputStream = FileOutputStream(tempFile)
                FLog.d(TAG, "makeThumbnails(): Created tempFile $tempFile")

                val dataSource = pipeline!!.fetchEncodedImage(imageRequest, this)
                val resized = synchronousThumbnailDataSourceToDisk(
                        imageFile,
                        intermediateResizeOutputStream,
                        dataSource,
                        resizeOptions,
                        quality
                )

                var imageToEncrypt: File? = null
                if (resized) {
                    FLog.d(TAG, "makeThumbnails(): Finished resizing $tempFile")
                    scanFile(tempFile.absolutePath)
                    imageToEncrypt = tempFile
                } else {
                    imageToEncrypt = imageFile
                }
                if (imageToEncrypt.length() > 0) {
                    // Resizing finished, now encrypt the resized image
                    val encryptedImageFile = synchronousEncryptEtcImage(imageToEncrypt)

                    if (encryptedImageFile != null) {
                        val fileSizeCsvRow = "CSV: ${imageToEncrypt.name},${imageToEncrypt.length()},${encryptedImageFile.length()},${encryptedImageFile.length() * 1.0 / imageToEncrypt.length()}"
                        FLog.d(TAG, fileSizeCsvRow)
                        FLog.d(TAG, "makeThumbnails(): Done encrypting etc thumbnail $tempFile")
                    } else {
                        FLog.d(TAG, "makeThumbnails(): Did not encrypt etc thumbnail $tempFile")
                    }
                } else {
                    FLog.e(TAG, "makeThumbnails(): Did not encrypt etc thumbnail $imageToEncrypt because length=${imageToEncrypt.length()}")
                }
            }
        }
    }

    private fun encryptEtcFiles(files: List<File>) {
        GlobalScope.launch(Dispatchers.Main) {
            // Process only one image at a time
            for (imageFile in files) {
                scanFile(imageFile.absolutePath)
                if (imageFile.nameWithoutExtension.contains("encrypted")) {
                    continue
                }
                FLog.d(TAG, "encryptEtcFiles(): Encrypting etc image $imageFile")
                val encryptedImageFile = synchronousEncryptEtcImage(imageFile)

                if (encryptedImageFile != null) {
                    val fileSizeCsvRow = "CSV: ${imageFile.name},${imageFile.length()},${encryptedImageFile.length()},${encryptedImageFile.length() * 1.0 / imageFile.length()}"
                    FLog.d(TAG, fileSizeCsvRow)
                    FLog.d(TAG, "encryptEtcFiles(): Done encrypting etc image $imageFile")
                } else {
                    FLog.d(TAG, "encryptEtcFiles(): Did not encrypt etc image $imageFile")
                }
            }
        }
    }

    private suspend fun synchronousEncryptEtcImage(imageFile: File): File? = suspendCoroutine { cont ->
        encryptEtcImage(imageFile) {
            cont.resume(it)
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
        decryptDataSourceToDisk(dataSource, null, null, null, factory, outputFile, callback)
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

    private fun constructDecryptImageRequest(image: File): ImageRequest {
        val fileUri = Uri.fromFile(image)
        return ImageRequestBuilder.newBuilderWithSource(fileUri)
                .setDecrypt(true)
                .setJpegCryptoKey(lastKey)
                .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                .build()
    }

    private fun decryptEtcImage(trio: ImageTrio, callback: (encryptedFile: File) -> Unit) {
        val outputDir = File(trio.redFile.parentFile, "decrypted")
        outputDir.mkdirs()
        val outputFile = outputDir.resolve("${trio.redFile.nameWithoutExtension.replace("red", "decrypted")}.${trio.redFile.extension}")

        if (outputFile.exists() && outputFile.length() > 0) {
            FLog.d(TAG, "Decrypted image ${outputFile.name} already exists, skipping decryption")
            scanFile(outputFile.absolutePath)
            callback(outputFile)
            return
        }

        pipeline!!.clearCaches()
        setNewKey(true)
        val imageRequestRed = constructDecryptImageRequest(trio.redFile)
        val imageRequestGreen = constructDecryptImageRequest(trio.greenFile)
        val imageRequestBlue = constructDecryptImageRequest(trio.blueFile)

        val dataSourceRed = pipeline!!.fetchEncodedImage(imageRequestRed, this)
        val dataSourceGreen = pipeline!!.fetchEncodedImage(imageRequestGreen, this)
        val dataSourceBlue = pipeline!!.fetchEncodedImage(imageRequestBlue, this)

        val factory = NativeImageDecryptorFactory.getNativeImageDecryptorFactory()

        FLog.d(TAG, "decryptImage() writing to output file: $outputFile")
        decryptDataSourceToDisk(dataSourceRed, dataSourceGreen, dataSourceBlue, null, factory, outputFile, callback)
    }


    private fun decryptEtcFiles(files: List<File>) {
        val imageTrios = mutableListOf<ImageTrio>()
        val red = "red"

        for (imageFile in files) {
//            if (!imageFile.nameWithoutExtension.contains("encrypted")) {
//                continue
//            }

            if (imageFile.nameWithoutExtension.contains("red")) {
                val greenFile = File(imageFile.absolutePath.replace(red, "green"))
                val blueFile = File(imageFile.absolutePath.replace(red, "blue"))

                if (imageFile.exists() && greenFile.exists() && blueFile.exists()) {
                    imageTrios.add(ImageTrio(imageFile, greenFile, blueFile))
                } else {
                    FLog.e(TAG, "Missing encrypted files: $imageFile / $greenFile / $blueFile")
                    continue
                }
            }
        }
        GlobalScope.launch(Dispatchers.Main) {
            // Process only one image at a time
            for (trio in imageTrios) {
                FLog.d(TAG, "Decrypting image $trio")
                val resultFile = synchronousDecryptEtcFiles(trio)
                FLog.d(TAG, "Finished decrypting image $trio (resultFile=$resultFile")
            }
        }
    }

    private suspend fun synchronousDecryptEtcFiles(trio: ImageTrio): File = suspendCoroutine { cont ->
        decryptEtcImage(trio) {
            cont.resume(it)
        }
    }

    private fun scanFile(path: String) {
        MediaScannerConnection.scanFile(context!!.applicationContext, arrayOf(path), null, msClient)
    }

    private suspend fun synchronousThumbnailDataSourceToDisk(imageFile: File,
                                                             intermediateResizeOutputStream: OutputStream,
                                                             dataSource: DataSource<CloseableReference<PooledByteBuffer>>,
                                                             resizeOptions: ResizeOptions,
                                                             quality: Int): Boolean = suspendCoroutine { cont ->
        thumbnailDataSourceToDisk(imageFile, intermediateResizeOutputStream, dataSource, resizeOptions, quality) {
            cont.resume(it)
        }
    }

    private fun thumbnailDataSourceToDisk(imageFile: File,
                                          intermediateResizeOutputStream: OutputStream,
                                          dataSource: DataSource<CloseableReference<PooledByteBuffer>>,
                                          resizeOptions: ResizeOptions,
                                          quality: Int,
                                          operationCompleteCallback: (imageWasResized: Boolean) -> Unit) {
        val executor = DefaultExecutorSupplier(1).forBackgroundTasks()
        val transcoderFactory = NativeImageTranscoderFactory.getNativeImageTranscoderFactory(pipeline!!.config.experiments.maxBitmapSize, true)

        val dataSubscriber = object : BaseDataSubscriber<CloseableReference<PooledByteBuffer>>() {
            override fun onNewResultImpl(
                    dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                if (!dataSource.isFinished) {
                    // if we are not interested in the intermediate images,
                    // we can just return here.
                    return
                }

                if (dataSource.result == null) {
                    Log.w(TAG, "DataSource result was null")
                    return
                }

                try {
                    val encodedImage = EncodedImage(dataSource.result)
                    val resized = if (encodedImage.width <= 2 * resizeOptions.width && encodedImage.height <= 2 * resizeOptions.height) {
                        // Skip making thumbnail if the image is small enough
                        Log.d(TAG, "thumbnailDataSourceToDisk Skipping thumbnail generation: (${resizeOptions.width} x ${resizeOptions.height})")
                        false
                    } else {
                        // The image is bigger than the desired thumbnail size
                        val transcoder = transcoderFactory.createImageTranscoder(encodedImage.imageFormat, true)
                        if (transcoder != null) {
                            try {
                                transcoder.transcode(encodedImage,
                                        intermediateResizeOutputStream,
                                        null,
                                        resizeOptions,
                                        encodedImage.imageFormat,
                                        quality)
                                Log.d(TAG, "Wrote resized JPG: ${imageFile.name} (${encodedImage.width} x ${encodedImage.height} to ${resizeOptions})")
                            } catch (e: IllegalArgumentException) {
                                Log.e(TAG, "Requested to make thumbnail for an image which is smaller than the thumbnail: $encodedImage")
                            }
                        } else {
                            Log.w(TAG, "thumbnailDataSourceToDisk() got encodedImage which was not a supported type: ${encodedImage.imageFormat} (${imageFile.name})")
                        }
                        Log.d(TAG, "dataSourceToDisk imageOperation with dimensions: ${encodedImage.width} x ${encodedImage.height}")
                        true
                    }

                    encodedImage.close()
                    operationCompleteCallback(resized)
                } catch (e: IOException) {
                    Log.e(TAG, "IOException while trying to write encrypted JPEG to disk", e)
                } finally {
                    CloseableReference.closeSafely(dataSource.result)
                    Closeables.close(intermediateResizeOutputStream, true)
                }
            }

            override fun onFailureImpl(dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                Log.e(TAG, "Failed to load and make thumbnail for ${imageFile.name}", dataSource.failureCause)
            }
        }

        dataSource.subscribe(dataSubscriber, executor)
    }

    private fun encryptDataSourceToDisk(dataSource: DataSource<CloseableReference<PooledByteBuffer>>,
                                        viewToDisplayWith: SimpleDraweeView?,
                                        encryptorFactory: ImageEncryptorFactory,
                                        outputFile: File? = null,
                                        outputFileGreen: File? = null,
                                        outputFileBlue: File? = null,
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
                    var fileOutputStream: OutputStream? = null
                    var fileOutputStreamGreen: OutputStream? = null
                    var fileOutputStreamBlue: OutputStream? = null
                    try {
                        val encodedImage = EncodedImage(ref)
                        val uriAsFile = File(mUri!!.lastPathSegment)
                        outputImageFile = outputFile
                                ?: File.createTempFile(uriAsFile.nameWithoutExtension, ".jpg", encryptedImageDir)

                        fileOutputStream = FileOutputStream(outputImageFile)

                        try {
                            val encryptor = encryptorFactory.createImageEncryptor(encodedImage.imageFormat)
                                    ?: throw java.lang.RuntimeException("encryptor is null, $outputFile, encodedImage.imageFormat=${encodedImage.imageFormat}")

                            if (outputFileGreen != null && outputFileBlue != null) {
                                fileOutputStreamGreen = FileOutputStream(outputFileGreen)
                                fileOutputStreamBlue = FileOutputStream(outputFileBlue)

                                FLog.d(TAG, "encryptDataSourceToDisk(): encrypting etc ${outputImageFile!!.name}")
                                val encryptTime = measureTimeMillis {
                                    encryptor.encryptEtc(encodedImage, fileOutputStream, fileOutputStreamGreen, fileOutputStreamBlue, lastKey, JPEG_QUALITY)
                                }
                                FLog.d(TAG, "encryptDataSourceToDisk(): encrypt etc time: $encryptTime / length = ${outputImageFile.length()}")

                                if (outputImageFile.length() > 0) {
                                    FLog.d(TAG, "Wrote %s encryptEtc Red to %s (size: %s bytes)", mUri, outputImageFile.absolutePath, outputImageFile.length() / 8)
                                    FLog.d(TAG, "Wrote %s encryptEtc Green to %s (size: %s bytes)", mUri, outputFileGreen.absolutePath, outputFileGreen.length() / 8)
                                    FLog.d(TAG, "Wrote %s encryptEtc Blue to %s (size: %s bytes)", mUri, outputFileBlue.absolutePath, outputFileBlue.length() / 8)
                                    FLog.d(TAG, "encryptEtc encryptTime=$encryptTime ($outputImageFile)")
                                    scanFile(outputFileGreen.absolutePath)
                                    scanFile(outputFileBlue.absolutePath)
                                } else {
                                    outputFile!!.delete()
                                    outputFileGreen.delete()
                                    outputFileBlue.delete()
                                    throw RuntimeException("Failed to encrypt JPEG, file size = 0: $outputImageFile")
                                }
                            } else {
                                encryptor.encrypt(encodedImage, fileOutputStream, lastKey)
                                FLog.d(TAG, "Wrote %s encrypted to %s (size: %s bytes)", mUri, outputImageFile!!.absolutePath, outputImageFile.length() / 8)
                            }

                            scanFile(outputImageFile.absolutePath)
                        } catch (e: IOException) {
                            throw RuntimeException("IOException while trying to write encrypted JPEG to disk", e)
                        }
                    } finally {
                        CloseableReference.closeSafely(ref)
                        Closeables.close(fileOutputStream, true)
                        Closeables.close(fileOutputStreamGreen, true)
                        Closeables.close(fileOutputStreamBlue, true)
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
                outputFile!!.delete()
                outputFileGreen!!.delete()
                outputFileBlue!!.delete()
                throw RuntimeException("Failed to load and encrypt/decrypt JPEG: $outputFile , $outputFileGreen , $outputFileBlue", t)
            }
        }

        dataSource.subscribe(dataSubscriber, executor)
    }

    private fun handleDataSourceResult(ref: CloseableReference<PooledByteBuffer>,
                                       refGreen: CloseableReference<PooledByteBuffer>?,
                                       refBlue: CloseableReference<PooledByteBuffer>?,
                                       viewToDisplayWith: SimpleDraweeView?,
                                       decryptorFactory: ImageDecryptorFactory,
                                       outputFile: File? = null,
                                       callback: ((encryptedFile: File) -> Unit)? = null) {
        var outputImageFile: File? = null

        try {
            val encodedImage = EncodedImage(ref)

            try {
                val uriAsFile = File(mUri!!.lastPathSegment)
                outputImageFile = outputFile
                        ?: File.createTempFile(uriAsFile.nameWithoutExtension, ".jpg", encryptedImageDir)

                val fileOutputStream = FileOutputStream(outputImageFile)

                val decryptor = decryptorFactory.createImageDecryptor(encodedImage.imageFormat)

                if (refGreen != null && refBlue != null) {
                    val encodedImageGreen = EncodedImage(refGreen)
                    val encodedImageBlue = EncodedImage(refBlue)
                    FLog.d(TAG, "decryptDataSourceToDisk invoking decryptEtc()")
                    CRYPTO_LOCK.withLock {
                        val decryptTime = measureTimeMillis {
                            decryptor.decryptEtc(encodedImage, encodedImageGreen, encodedImageBlue, fileOutputStream, lastKey)
                        }

                        FLog.d(TAG, "decryptDataSourceToDisk decryptEtc() decryptTime=$decryptTime ($outputFile)")
                    }
                    encodedImage.close()
                    encodedImageGreen.close()
                    encodedImageBlue.close()
                } else {
                    decryptor.decrypt(encodedImage, fileOutputStream, lastKey)
                }
                FLog.d(TAG, "Wrote %s decrypted to %s (size: %s bytes)", mUri, outputImageFile!!.absolutePath, outputImageFile.length() / 8)

                scanFile(outputImageFile.absolutePath)

                Closeables.close(fileOutputStream, true)
            } catch (e: IOException) {
                FLog.e(TAG, "IOException while trying to write encrypted JPEG to disk", e)
            }
        } finally {
            CloseableReference.closeSafely(ref)
            CloseableReference.closeSafely(refGreen)
            CloseableReference.closeSafely(refBlue)
        }

        if (outputImageFile != null) {
            lastSavedImage = Uri.parse(outputImageFile.toURI().toString())

            if (viewToDisplayWith != null) {
                val imageRequest = ImageRequestBuilder.newBuilderWithSource(lastSavedImage)
                        .setImageDecodeOptions(ImageDecodeOptionsBuilder.newBuilder().build())
                        .build()
                viewToDisplayWith.setImageRequest(imageRequest)
            }

            if (callback != null) {
                callback(outputImageFile)
            }
        }
    }

    private fun decryptDataSourceToDisk(dataSource: DataSource<CloseableReference<PooledByteBuffer>>,
                                        dataSourceGreen: DataSource<CloseableReference<PooledByteBuffer>>?,
                                        dataSourceBlue: DataSource<CloseableReference<PooledByteBuffer>>?,
                                        viewToDisplayWith: SimpleDraweeView?,
                                        decryptorFactory: ImageDecryptorFactory,
                                        outputFile: File? = null,
                                        callback: ((encryptedFile: File) -> Unit)? = null) {
        val executor = DefaultExecutorSupplier(1).forBackgroundTasks()

        // This is super ugly but idk how to wait for multiple datasources
        val dataSubscriber = object : BaseDataSubscriber<CloseableReference<PooledByteBuffer>>() {
            override fun onNewResultImpl(
                    dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                val ref = dataSource.result

                if (ref != null) {
                    if (dataSourceGreen == null && dataSourceBlue == null) {
                        handleDataSourceResult(ref, null, null, viewToDisplayWith, decryptorFactory, outputFile, callback)
                    } else {
                        val dataSubscriberGreen = object : BaseDataSubscriber<CloseableReference<PooledByteBuffer>>() {
                            override fun onNewResultImpl(
                                    dataSourceGreenResult: DataSource<CloseableReference<PooledByteBuffer>>) {
                                val refGreen = dataSourceGreenResult.result

                                if (refGreen != null) {
                                    val dataSubscriberBlue = object : BaseDataSubscriber<CloseableReference<PooledByteBuffer>>() {
                                        override fun onNewResultImpl(
                                                dataSourceBlueResult: DataSource<CloseableReference<PooledByteBuffer>>) {
                                            val refBlue = dataSourceBlueResult.result

                                            if (refBlue != null) {
                                                handleDataSourceResult(ref, refGreen, refBlue, viewToDisplayWith, decryptorFactory, outputFile, callback)
                                            }
                                        }

                                        override fun onFailureImpl(dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                                            val t = dataSource.failureCause
                                            FLog.e(TAG, "Failed to load blueRef", t)
                                        }
                                    }

                                    dataSourceBlue?.subscribe(dataSubscriberBlue, executor)
                                }
                            }

                            override fun onFailureImpl(dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                                val t = dataSource.failureCause
                                FLog.e(TAG, "Failed to load greenRef", t)
                            }
                        }

                        dataSourceGreen?.subscribe(dataSubscriberGreen, executor)
                    }
                }
            }

            override fun onFailureImpl(dataSource: DataSource<CloseableReference<PooledByteBuffer>>) {
                val t = dataSource.failureCause
                FLog.e(TAG, "Failed to load decrypt ref", t)
            }
        }

        dataSource.subscribe(dataSubscriber, executor)
    }

    override fun getTitleId(): Int {
        return R.string.drawee_encrypt_title
    }

    companion object {
        private val CRYPTO_LOCK = ReentrantLock()
    }

    data class ImageTrio(var redFile: File,
                         var greenFile: File,
                         var blueFile: File)
}
