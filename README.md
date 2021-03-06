MeshUp - A Copyright (c) 2012-2018 Martin Felis <martin.felis@iwr.uni-heidelberg.de>

# Introduction

MeshUp is a visualization tool for multibody systems based on skeletal
animation and magic. It renders models in real-time and allows to render
motions directly to videos or image sequences.

# Features:

  * Simple definition of models (see directory models/ for examples)
  * Model serialization to and from Lua files
  * Lading of Wavefront OBJ files that can be attached to the meshes
  * Customizable frame definitions order of rotation angles
  * Loading and interpolation of keyframed animation (see sampleanimation.txt)
  * Direct rendering of videos or image sequences
	* Lua scripting
  * Rendering of forces and torques arrows

# Usage:

	meshup [model_file(s)] [animation_file(s)] [force_file(s)]

See meshup --help for more options.

# Installation

## Prepackaged versions

  * [for Arch Linux, there is a package available in the AUR](https://aur.archlinux.org/packages/meshup/)

## Building from source

### Dependencies and Required Programs

For Ubuntu the following packages are required:

	* cmake
	* qt5-default
	* libboost-dev
	* libboost-filesystem-dev
	* libboost-system-dev
	* libavcodec-dev libavformat-dev libswresample-dev libswscale-dev

### Build
	* clone repository 
		* (github) -> https://github.com/ORB-HD/MeshUp
		* (git rhodecode) -> https://[username]@orb.iwr.uni-heidelberg.de/code/software/meshup/meshup
		* (mercurial) -> https://[username]@orb.iwr.uni-heidelberg.de/code/software/meshup/meshup-qt5-legacy
	* create build folder in meshup code directory
In build Directory
	* cmake ..
	* make

### Install
	* make install

# File Format Documentation

The different file formats used in MeshUp are described in
doc/FormatDescriptions.md.

# Model Files

MeshUp tries to find the model file by searching for a file called
"<model_name>" or "<model_name>.lua". It first checks in the local
directory "./" and "./models/". After that MeshUp checks whether the
environment variable MESHUP_PATH is set and checks for the file in
$MESHUP_PATH/ or $MESHUP_PATH/models.

Please note that as default MeshUp interprets all angular values as degree
values, not radians.

# Meshes

Similar as in storing [Model Files](#markdown-header-model-files), MeshUp
tries to find meshes in various places. Unlike for models, meshes are
required to be in a subfolder called "meshes/".

See [Notes](#markdown-header-notes) further down for information on how to
export meshes to OBJ files that can be included directly into Meshup.

# Animation Files

Animation files are designed so that they can be written to a comma or tab
separated file and still be read by MeshUp. There is a small number of
keywords that specify how the data is being interpreted.

There are two sections in the animation file: COLUMNS and DATA.

## COLUMNS section

The COLUMNS section specifies the separate columns of the file and is
started with a line that only contains "COLUMNS:". Each following entry
that may be separated by commas or whitespaces specifies a single degree of
freedom for a frame or the time. The first entry corresponds to the first
column, the second for the second and so on.

The first column must always be "time". If a column should be
ignored by MeshUp set the entry to "empty".

The mapping for joints is specified in the following syntax:

	<frame name>:<joint type>:<axis>[:<unit>]

where

	<frame name> is the name of the frame used in the model.
	<joint type> can be 
	  t,translation for translational motions
	  r,rotation    for rotational motions
	  s,scale       for scaling motions
	<axis> can be either of x,y,z for the respective axes. Negative axes can
	  be specified by prepending a '-' to the axis name.
	<unit> (optional) can be r,rad, or radian to specify that the columns
	  should be interpreted as radians instead of the default degrees.

For a single frame all column specifications must be consecutive in the
COLUMN section.

## DATA section

The DATA section has to be specified as multi-column data of the raw values
for which each column is separated by a "," (comma) and at least one
	whitespace (space or tab).

The section either starts with "DATA:" and directly after it the data or
alternatively, one can use "DATA_FROM: <some_path_to_filename>" to load the
actual data from another data file. This can be useful when wanting to keep
the actual data as a clear .csv file or if one wants to re-use the header
of a file.

See sampleanimation.txt for an example.

# Forces and Torques Files

Forces and Torques Files are CSV files with the file extention ".ff". The 
first column specifiese the time of the following data. After the time column,
each next 9 columns represent one force/torque entry. The first 3 columns of the 9 contain the
postion of the force and torque. The next 3 columns are the force data and the 3
columns after are the torque data.

# Notes

Wavefront OBJ restrictions:

  * Faces of the model must be triangles
  * Textures are not supported
  * Materials are not supported

Exporting Meshes from Blender:

When exporting meshes as Wavefront OBJ files from Blender, make sure to
apply the following settings:

	  [X] Selection Only
	  [X] Apply Modifiers
	  [X] Include Edges
	  [X] Include Normals
	  [X] Triangulate Faces
	  [X] Objects as OBJ Objects
	  Forward   : -X Forward
	  Up        :  Y Up
	  Path Mode : Auto

You can save these settings as "Operator Presets" to simplify the export
in the future.

# Bugs

Please use the bug tracker at [https://bitbucket.org/MartinFelis/meshup/issues](https://bitbucket.org/MartinFelis/meshup/issues) to report or view fixed bugs.

# License

meshup is published under the MIT license. However Meshup makes use of
other libraries such as Qt, GLEW, jsoncpp, and Lua for which the actual
license may differ.

	Copyright © 2012 Martin Felis <martin.felis@iwr.uni-heidelberg.de>
	
	Permission is hereby granted, free of charge, to any person obtaining a
	copy of this software and associated documentation files (the "Software"),
	to deal in the Software without restriction, including without limitation
	the rights to use, copy, modify, merge, publish, distribute, sublicense,
	and/or sell copies of the Software, and to permit persons to whom the
	Software is furnished to do so, subject to the following conditions:
	
	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.
	
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
	DEALINGS IN THE SOFTWARE. 
