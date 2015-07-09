% grt-pack
% 
% 

# NANE

 grt-unpack - merge an audio stream and subtitle file into a csv stream for classification

# SYNOPSIS
 grt unpack [-h|--help] \<file\> [file]
            [-t|--stream] \<streamid\> [streamid]
            [-s|--style] \<style\>
            [-d|--default] \<label\>
            [-n|--no-flush]

# DESCRIPTION

 This is a wrapper around ffmpeg for unpacking sensor data for classification with the grt tools. In principle this program merges a subtitle stream with an audio stream, converts it to floats in text format and prints data in "label channel_1 channel_2 ... channel_n" format. 

 Files can be any subtitle file, audio and video file, anything ffmpeg can handle. Audio streams can be selected by their language or name identification tag, if neccesary. Otherwise all audio tracks will be unpacked. If multiple audio tracks exists in the input, they will be resampled to their highest common frequency with the sox audio resampling module. All audio tracks will be merged into one frame. A file can also be left out or specified as '-', in which the standard input is read as a file, which is useful for doing streams.

# OPTIONS
-h, --help
:   Print a help message. 

-t, --stream \<streamid\> [streamid]
:   one or more stream id's matching the language tag to be used

-s, --style \<style\>
:   select only subtitles with this styles, default is to select all

-d, --default \<label\>
:   the label to emit when no label is in the subtitle, default: NULL
 
# EXAMPLES