FROM gcc:14-bookworm AS native
WORKDIR /build
COPY fastio.cpp .
RUN g++ -O3 -DNDEBUG -std=c++17 -static-libstdc++ -static-libgcc fastio.cpp -o /fastio

FROM python:3.12-slim-bookworm
RUN pip install --no-cache-dir duckdb==1.5.6 polars==1.38.1
WORKDIR /app
COPY --from=native /fastio /app/fastio
COPY pipeline.py /app/pipeline.py
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
CMD ["python", "/app/pipeline.py"]
