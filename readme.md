# OrientView

OrientView is an orienteering video analyzing program which displays the video and the map side-by-side in real-time. Almost all image and video file formats are supported. Routes are created and calibrated using [QuickRoute](http://www.matstroeng.se/quickroute/en/). Resulting video can also be exported to an H.264 encoded MP4 video file.

* Author: [Mikko Ronkainen](http://mikkoronkainen.com)
* Website: [github.com/mikoro/orientview](https://github.com/mikoro/orientview)

[![Travis Status](https://travis-ci.org/mikoro/orientview.svg?branch=master)](https://travis-ci.org/mikoro/orientview)
[![Coverity Status](https://scan.coverity.com/projects/2849/badge.svg)](https://scan.coverity.com/projects/2849)

![Screenshot](http://mikoro.github.io/images/orientview/readme-screenshot.jpg "Screenshot")

## Download

Download the latest version:

| Windows 64-bit                                                                                                         | Mac OS X          | Linux                                                            |
|------------------------------------------------------------------------------------------------------------------------|-------------------|------------------------------------------------------------------|
| [OrientView-1.0.0-Setup.msi](https://github.com/mikoro/orientview/releases/download/v1.0.0/OrientView-1.0.0-Setup.msi) | Not yet available | [Arch Linux AUR](https://aur.archlinux.org/packages/orientview/) |
| [OrientView-1.0.0.zip](https://github.com/mikoro/orientview/releases/download/v1.0.0/OrientView-1.0.0.zip)             | &nbsp;            | [Build instructions](#linux_build)                               |

For testing out the program, you can also [download test data](https://mega.co.nz/#F!nM1gHbZJ!pvFqf3UOrHrmMyuUTlMSrg).

## Features

* Opens all the image formats supported by [Qt](http://qt-project.org/doc/qt/QImage.html#reading-and-writing-image-files).
* Plays all the video files that are supported by [FFmpeg](https://www.ffmpeg.org/general.html#Supported-File-Formats_002c-Codecs-or-Features).
* Reads route point and calibration data from JPEG images created with [QuickRoute](http://www.matstroeng.se/quickroute/en/).
* Split times are input manually using simple formatting (absolute or relative).
* Supports basic video seeking and pausing. Different timing offsets are also adjustable to make the video and route match.
* Differents parts of the UI are fully adjustable.
* Draws all the graphics using OpenGL. Also utilizes shaders to do custom image resampling (e.g. high quality bicubic).
* Video window (and exported video) is completely resizable - original video resolution does not pose any restrictions.
* Resulting video can be exported to MP4 format with H.264 encoding.
* Program architecture is multithreaded and should allow maximal CPU core usage when for example exporting video.
* Supports basic video stabilization using the [OpenCV](http://opencv.org/) library. Stabilization can be done real-time or by using preprocessed data.

## Instructions

### Workflow

* You need the video of the run, the map, the gps track, and the split times.
* If the video is in multiple parts, you need to stitch them together using e.g. [Avidemux](http://fixounet.free.fr/avidemux/).
* Scan the map with high resolution (600 dpi TIFF format is preferrable).
* Fix the map (orientation, cropping, levels etc.) and export one version with the original resolution (TIFF format preferrable) and export a smaller version for use with QuickRoute. Modern GPUs can easily take in 8192x8192 250 MB TIFF image - so there is no need to scale down or compress the map image that gets sent to the GPU. The QuickRoute image data will not be used so it's quality doesn't matter (only data inserted by QuickRoute to the JPEG file headers is used).
* Using [QuickRoute](http://www.matstroeng.se/quickroute/en/) cut and align the gps track to the map. You can use as many aligment points as you want. Then export the map as a JPEG image.
* Format the split times to a single string. Format is "hours:minutes:seconds" with hours and minutes being optional and the separator between splits being "|" or ";". For example: `0:00|1:23|1:23:45`. Time separator can also be ".". For example: `0.00;1.23;1.23.45`. Split times can be absolute or relative.
* Open OrientView and select the map image file, the QuickRoute JPEG file, and the video file. Input the split times and press Play!

### Controls

| Key           | Action                                                     |
|---------------|------------------------------------------------------------|
| **F1**        | Toggle info panel on/off                                   |
| **F2**        | Select map/video/none for scrolling                        |
| **F3**        | Select render mode (map/video/all)                         |
| **F4**        | Select route render mode (normal/pace/none)                |
| **F5**        | Toggle runner on/off                                       |
| **F6**        | Toggle controls on/off                                     |
| **F7**        | Toggle video stabilizer on/off                             |
| **Space**     | Pause or resume video <br> Ctrl + Space advances one frame |
| **Ctrl**      | Slow/small modifier                                        |
| **Shift**     | Fast/large modifier                                        |
| **Alt**       | Very fast/large modifier                                   |
| **Backspace** | Reset all user transformations to default                  |
| **Left**      | Seek video backwards <br> Scroll map/video left            |
| **Right**     | Seek video forwards <br> Scroll map/video right            |
| **Up**        | Scroll map/video up                                        |
| **Down**      | Scroll map/video down                                      |
| **Q**         | Zoom map in                                                |
| **A**         | Zoom map out                                               |
| **W**         | Rotate map counterclockwise                                |
| **S**         | Rotate map clockwise                                       |
| **E**         | Increase map width                                         |
| **D**         | Decrease map width                                         |
| **R**         | Zoom video in                                              |
| **F**         | Zoom video out                                             |
| **T**         | Rotate video counterclockwise                              |
| **G**         | Rotate video clockwise                                     |
| **Page Up**   | Increase runner offset                                     |
| **Page Down** | Decrease runner offset                                     |
| **Home**      | Increase control offset                                    |
| **End**       | Decrease control offset                                    |
| **Insert**    | Increase route scale                                       |
| **Delete**    | Decrease route scale                                       |

### Misc

* Most of the UI controls have tooltips explaining what they are for.
* Not all settings are exposed to the UI. You can edit the extra settings by first saving the current settings to a file, open it with a text editor (the file is in ini format), do some modifications, and then load the file back.
* The difference between real-time and preprocessed stabilization is that the latter can look at the future when doing the stabilization analysis. This makes the centering faster with sudden large frame movements and also makes the stabilization a little bit more responsive to small movements.

### Building on Windows

1. Install [Visual Studio 2013 Professional](http://www.visualstudio.com/).
2. Install [Qt 5.3](http://qt-project.org/downloads) (64-bit, VS 2013, OpenGL).
1. Clone [https://github.com/mikoro/orientview.git](https://github.com/mikoro/orientview.git).
2. Clone [https://github.com/mikoro/orientview-binaries.git](https://github.com/mikoro/orientview-binaries.git).
3. Copy the files from orientview-binaries/windows to the orientview root folder.
4. Build using either Visual Studio or QtCreator.

### <a name="linux_build"></a>Building on Linux

1. Install [Qt 5.3](http://qt-project.org/).
2. Install [FFmpeg 2.3](https://www.ffmpeg.org/).
3. Install [OpenCV 2.4](http://opencv.org/).
4. Install [x264](http://www.videolan.org/developers/x264.html).
5. Install [L-SMASH](https://github.com/l-smash/l-smash).
6. Clone [https://github.com/mikoro/orientview.git](https://github.com/mikoro/orientview.git).
7. Run `qmake && make`.

## License

    OrientView
    Copyright © 2014 Mikko Ronkainen
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
