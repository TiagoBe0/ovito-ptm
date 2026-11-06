////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//

#pragma once


#include <ovito/core/Core.h>
#include <ovito/core/app/ApplicationService.h>

namespace Ovito {

/**
 * In-process test of the image and animation output path.
 *
 * Writes a PNG image and a 4-frame animated GIF (and an AVI where the ffmpeg build offers it) of a synthetic
 * gradient into a temporary directory and checks that each file has the right signature and a plausible size.
 * Fails if the GIF format is not offered by VideoEncoder::supportedFormats(), which is what happens when OVITO is
 * built without ffmpeg.
 *
 * Run with `ovito --nogui --video-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_CORE_EXPORT VideoSelfTestService : public ApplicationService
{
    OVITO_CLASS(VideoSelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the self-test after the application has entered the main event loop.
    virtual void applicationStarted() override;
};

}   // End of namespace
