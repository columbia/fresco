package com.facebook.imagepipeline.common;

import com.facebook.common.internal.Preconditions;

public class JpegCryptoKey {
  private String x0;
  private String mu;

  public JpegCryptoKey(String x0, String mu) {
    Preconditions.checkArgument(x0 != null && !x0.isEmpty(), "x0 cannot be empty or null");
    Preconditions.checkArgument(mu != null && !mu.isEmpty(), "mu cannot be empty or null");
    this.x0 = x0;
    this.mu = mu;
  }

  static public JpegCryptoKey getTestKey() {
    return new JpegCryptoKey("5.55555555555555555556e-1", "3.577777777777777777e0");
  }

  public String getX0() {
    return x0;
  }

  public String getMu() {
    return mu;
  }
}
