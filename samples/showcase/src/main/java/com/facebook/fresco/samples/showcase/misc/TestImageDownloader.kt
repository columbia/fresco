package com.facebook.fresco.samples.showcase.misc

import android.net.Uri
import com.facebook.common.logging.FLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.net.URL

class TestImageDownloader(private val downloadDir: File) {

    fun downloadFromList(listUrl: URL, downloadCompleteCallback: (imageFiles: List<File>) -> Unit) {
        GlobalScope.launch(context = Dispatchers.Main) {
            withContext(Dispatchers.IO) {
                val savedFiles = mutableListOf<File>()
                for (line in listUrl.readText().lines()) {
                    val imageUrl = URL(line)
                    val imageUri = Uri.parse(line)

                    FLog.d(TAG, "Got line: $line")
                    val downloadedImage = File(downloadDir, imageUri.lastPathSegment!!)

                    if (!downloadedImage.exists()) {
                        FLog.d(TAG, "$line didn't exist, saving to disk: $downloadedImage")
                        imageUrl.openStream().use { inputStream ->
                            downloadedImage.outputStream().use { outputStream ->
                                inputStream.copyTo(outputStream)
                            }

                        }
                    }
                    savedFiles.add(downloadedImage)
                }

                downloadCompleteCallback(savedFiles)
            }
        }
    }

    companion object {
        private const val TAG = "TestImageDownloader"
    }
}