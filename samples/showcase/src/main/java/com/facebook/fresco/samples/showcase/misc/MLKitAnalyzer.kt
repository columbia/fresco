package com.facebook.fresco.samples.showcase.misc

import android.content.Context
import android.net.Uri
import com.facebook.common.logging.FLog
import com.google.firebase.ml.vision.FirebaseVision
import com.google.firebase.ml.vision.common.FirebaseVisionImage
import java.io.IOException

class MLKitAnalyzer(private val context: Context) {

    fun analyze(filePath: Uri, labelerType: Labeler) {
        val image = try {
            FirebaseVisionImage.fromFilePath(context, filePath)
        } catch (e: IOException) {
            FLog.e("PDK", "Failed to create FirebaseVision image from $filePath", e)
            return
        }

        val labeler = when (labelerType) {
            Labeler.ON_DEVICE -> FirebaseVision.getInstance().onDeviceImageLabeler
            Labeler.CLOUD -> FirebaseVision.getInstance().cloudImageLabeler
        }

        labeler.processImage(image).addOnSuccessListener {
            FLog.d("PDK", "Successfully labeled $filePath ($it)")
        }.addOnFailureListener {
            FLog.e("PDK", "Failed to label $filePath", it)
        }
    }

    enum class Labeler {
        ON_DEVICE, CLOUD
    }
}