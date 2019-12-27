package com.facebook.imagepipeline.common;

import com.facebook.common.internal.Preconditions;

import java.security.SecureRandom;

public class JpegCryptoKey {
  private String x0;
  private String mu;

  private JpegCryptoKey(String x0, String mu) {
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

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;

    JpegCryptoKey that = (JpegCryptoKey) o;

    if (x0 != null ? !x0.equals(that.x0) : that.x0 != null) return false;
    return mu != null ? mu.equals(that.mu) : that.mu == null;
  }

  @Override
  public int hashCode() {
    int result = x0 != null ? x0.hashCode() : 0;
    result = 31 * result + (mu != null ? mu.hashCode() : 0);
    return result;
  }

  @Override
  public String toString() {
    return "JpegCryptoKey{" +
            "x0='" + x0 + '\'' +
            ", mu='" + mu + '\'' +
            '}';
  }

  public static class Builder {
    private static final int MIN_X0 = 5;
    private static final int MAX_X0 = 9;
    private static final int MIN_MU = 5;
    private static final int MAX_MU = 9;
    private String x0;
    private String mu;

    public JpegCryptoKey build() {
      Preconditions.checkArgument(x0 != null && !x0.isEmpty(), "x0 cannot be empty or null");
      Preconditions.checkArgument(mu != null && !mu.isEmpty(), "mu cannot be empty or null");
      return new JpegCryptoKey(x0, mu);
    }

    public Builder generateNewValues(final int x0Length, final int muLength) {
      final StringBuilder x0Builder = new StringBuilder();
      final StringBuilder muBuilder = new StringBuilder();
      final SecureRandom secRandom = new SecureRandom();

      // x0 is in [0.5, 1.0)
      x0Builder.append("0.");

      for (int i = 1; i < x0Length; i++) {
        if (i == 1) {
          x0Builder.append(secRandom.nextInt(MAX_X0 - MIN_X0 + 1) + MIN_X0);
        } else {
          x0Builder.append(secRandom.nextInt(10));
        }
      }

      // mu is in [3.57, 4.0)
      muBuilder.append("3.");

      for (int i = 1; i < muLength; i++) {
        if (i == 1) {
          muBuilder.append(secRandom.nextInt(MAX_MU - MIN_MU + 1) + MIN_MU);
        } else {
          muBuilder.append(secRandom.nextInt(10));
        }
      }

      this.x0 = x0Builder.append("e-1").toString();
      this.mu = muBuilder.append("e0").toString();

      return this;
    }

    public Builder setX0(final String x0) {
      this.x0 = x0;
      return this;
    }

    public Builder setMu(final String mu) {
      this.mu = mu;
      return this;
    }

    public String getX0() {
      return x0;
    }

    public String getMu() {
      return mu;
    }
  }
}
