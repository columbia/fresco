package com.facebook.fresco.samples.showcase.misc

import android.content.Context
import android.net.Uri
import com.facebook.common.logging.FLog
import com.google.firebase.ml.vision.FirebaseVision
import com.google.firebase.ml.vision.common.FirebaseVisionImage
import com.google.firebase.ml.vision.label.FirebaseVisionImageLabel
import org.json.JSONObject
import java.io.IOException

class MLKitAnalyzer(private val context: Context) {

    fun analyze(filePath: Uri, labelerType: Labeler, onSuccessListener: (imageLabels: List<FirebaseVisionImageLabel>) -> Unit) {
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

        labeler.processImage(image).addOnSuccessListener { imageLabels ->
            FLog.d(TAG, "Successfully labeled ${filePath.lastPathSegment}")
            for (label in imageLabels) {
                FLog.d(TAG, "Label: $${label.text}")
            }

            onSuccessListener(imageLabels)
        }.addOnFailureListener {
            FLog.e(TAG, "Failed to label $filePath", it)
        }
    }

    /**
     * {
     *   "image": "filename.jpg",
     *   "labels": ["label1", "label2", "label3"]
     * }
     */
    fun toJson(imageName: String, labels: List<FirebaseVisionImageLabel>): JSONObject {
        val jsonObj = JSONObject()

        jsonObj.put("image", imageName)

        for (label in labels) {
            val labelMap = mutableMapOf<String, String>()
            labelMap["text"] = label.text
            //labelMap["entityId"] = label.entityId
            labelMap["confidence"] = label.confidence.toString()

            jsonObj.accumulate("labels", labelMap)
        }

        return jsonObj
    }

    enum class Labeler {
        ON_DEVICE, CLOUD
    }

    companion object {
        private const val TAG = "MLKitAnalyzer"
    }
}