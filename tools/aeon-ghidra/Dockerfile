# Headless Ghidra 12.1.3 + ghidra-plugin-aeon (MStar AEON R2 processor module).
# Lets us disassemble the MST9U scaler AP without installing JDK 21 / Ghidra on
# the host. Static analysis only; the target firmware is never executed.
#
# Build inputs (fetched + SHA-pinned by build.sh, placed in the build context):
#   ghidra.zip  Ghidra 12.1.3 PUBLIC
#     https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.3_build/ghidra_12.1.3_PUBLIC_20260817.zip
#   aeon.zip    ghidra-plugin-aeon v0.1.0 (built for Ghidra 12.1.3)
#     https://github.com/eb4x/ghidra-plugin-aeon/releases/download/v0.1.0/ghidra_12.1.3_PUBLIC_20260918_AeonR2.zip
FROM eclipse-temurin:21-jdk

ARG GHIDRA_SHA=93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54
ARG AEON_SHA=d76c6a914cf876e9913c0d4c3596c2280d0a8f08bc08dd02fdf8003304562863

RUN apt-get update \
 && apt-get install -y --no-install-recommends unzip fontconfig libfreetype6 \
 && rm -rf /var/lib/apt/lists/*

COPY ghidra.zip aeon.zip /tmp/
RUN echo "${GHIDRA_SHA}  /tmp/ghidra.zip" | sha256sum -c - \
 && echo "${AEON_SHA}  /tmp/aeon.zip"     | sha256sum -c - \
 && unzip -q /tmp/ghidra.zip -d /opt \
 && mv /opt/ghidra_* /opt/ghidra \
 && unzip -q /tmp/aeon.zip -d /opt/ghidra/Ghidra/Extensions \
 && rm /tmp/ghidra.zip /tmp/aeon.zip

ENV GHIDRA=/opt/ghidra
COPY scripts/ /opt/scripts/

# Analysis runs as this non-root user; mounts are read-only for input.
RUN useradd -m analyst && mkdir -p /work && chown analyst /work
USER analyst
WORKDIR /work
ENTRYPOINT ["/opt/ghidra/support/analyzeHeadless"]
