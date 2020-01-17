package com.facebook.fresco.samples.showcase.misc

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.net.URL

class TestImageDownloader(private val downloadDir: File) {

    fun downloadFromList(listUrl: URL, downloadCompleteCallback: (imageFiles: List<File>) -> Unit) {
        GlobalScope.launch(context = Dispatchers.Main) {
            val result = withContext(Dispatchers.IO) {
                val savedFiles = mutableListOf<File>()
                for (line in listUrl.readText().lines()) {
                    val imageUrl = URL(line)

                    imageUrl.openStream().use {
                        val downloadedImage = File.createTempFile(imageUrl.file, ".jpg", downloadDir)

                        savedFiles.add(downloadedImage)
                    }
                }

                downloadCompleteCallback(savedFiles)
            }
        }

    }
}