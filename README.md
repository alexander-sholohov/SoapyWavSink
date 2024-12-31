# Soapy SDR plugin for stream out WAV files with IQ data


## Dependencies

* SoapySDR - https://github.com/pothosware/SoapySDR/wiki


```shell
sudo apt-get install build-essential
sudo apt-get install cmake

```

## Build with cmake

```shell
git clone https://github.com/alexander-sholohov/SoapyWavSink.git
cd SoapyAfedri
git submodule init
git submodule update
mkdir build
cd build
cmake ..
cmake --build . 
sudo cmake --install .
```

## Probing WavSink:

```shell
SoapySDRUtil --probe="driver=wav_sink,file_name=some_file_name_here.wav"
```

