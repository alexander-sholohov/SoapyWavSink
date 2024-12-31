# Soapy SDR plugin for stream out WAV files with IQ data

## What is this thing?

This SoapySDR driver "plays" IQ wav file in infinite loop. The main purpose - is to develop and test SDR software.
To read wav file [this](https://github.com/adamstark/AudioFile.git) library has been utilized.

## Dependencies

* SoapySDR - https://github.com/pothosware/SoapySDR/wiki


```shell
sudo apt-get install build-essential
sudo apt-get install cmake

```

## Build with cmake

```shell
git clone https://github.com/alexander-sholohov/SoapyWavSink.git
cd SoapyWavSink
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
# provide file name as device driver argument
SoapySDRUtil --probe="driver=wav_sink,file_name=some_file_name_here.wav"
# provide file name through environment variable
SOAPY_WAV_SINK_FILE="some_file_name_here.wav" SoapySDRUtil --probe="driver=wav_sink"
```

