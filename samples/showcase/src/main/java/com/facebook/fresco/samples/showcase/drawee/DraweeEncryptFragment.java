package com.facebook.fresco.samples.showcase.drawee;

import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.Nullable;

import com.facebook.drawee.view.SimpleDraweeView;
import com.facebook.fresco.samples.showcase.BaseShowcaseFragment;
import com.facebook.fresco.samples.showcase.R;
import com.facebook.fresco.samples.showcase.misc.ImageUriProvider;
import com.facebook.imagepipeline.common.ImageDecodeOptionsBuilder;
import com.facebook.imagepipeline.request.ImageRequest;
import com.facebook.imagepipeline.request.ImageRequestBuilder;

public class DraweeEncryptFragment extends BaseShowcaseFragment {

  private SimpleDraweeView mDraweeEncryptView;
  private SimpleDraweeView mDraweeDecryptView;
  private Uri mUri;

  @Nullable
  @Override
  public View onCreateView(
          LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
    return inflater.inflate(R.layout.fragment_drawee_encrypt, container, false);
  }

  @Override
  public void onViewCreated(View view, @Nullable Bundle savedInstanceState) {
    mUri = sampleUris().createSampleUri(ImageUriProvider.ImageSize.M);
    mDraweeEncryptView = view.findViewById(R.id.drawee_view);
    mDraweeDecryptView = view.findViewById(R.id.drawee_decrypt);

    setEncryptOptions();

    view.findViewById(R.id.btn_random_uri)
            .setOnClickListener(
                    new View.OnClickListener() {
                      @Override
                      public void onClick(View v) {
                        mUri = sampleUris().createSampleUri();
                        setEncryptOptions();
                      }
                    });

    view.findViewById(R.id.btn_decrypt_image)
            .setOnClickListener(
                    new View.OnClickListener() {
                      @Override
                      public void onClick(View v) {
                        mUri = sampleUris().createSampleUri();
                        setDecryptOptions();
                      }
                    });
  }

  private void setEncryptOptions() {
    ImageRequest imageRequest =
            ImageRequestBuilder.newBuilderWithSource(mUri)
                    .setEncrypt(true)
                    .setImageDecodeOptions(new ImageDecodeOptionsBuilder().build())
                    .build();
    mDraweeEncryptView.setImageRequest(imageRequest);
  }

  private void setDecryptOptions() {
    ImageRequest imageRequest =
            ImageRequestBuilder.newBuilderWithSource(mUri)
                    .setDecrypt(true)
                    .setImageDecodeOptions(new ImageDecodeOptionsBuilder().build())
                    .build();
    mDraweeDecryptView.setImageRequest(imageRequest);
  }

  @Override
  public int getTitleId() {
    return R.string.drawee_encrypt_title;
  }
}
