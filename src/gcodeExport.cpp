/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include <stdarg.h>
#include <stdio.h>

#include "gcodeExport.h"
#include "pathOrderOptimizer.h"
#include "timeEstimate.h"
#include "settings.h"
#include "utils/logoutput.h"

namespace cura {

#define GCODE_MAX_LENGTH 96
#define COLOR_MIX 2
#define COLOR_DOUBLE 3
#define COLOR_SINGLE 4
#define COLOR_LAYER 1

GCodeExport::GCodeExport()
    : currentPosition(0,0,0), startPosition(INT32_MIN,INT32_MIN,0)
{
    extrusionAmount = 0;
    extrusionAmountTmp = 0;
    extrusionAAmount = 0;
    extrusionBAmount = 0;
    extrusionPerMM = 0;
    retractionAmount = 4.5;
    minimalExtrusionBeforeRetraction = 0.0;
    extrusionAmountAtPreviousRetraction = -10000;
    extruderSwitchRetraction = 14.5;
    extruderNr = 0;
    currentFanSpeed = -1;
    
    totalPrintTime = 0.0;
    for(unsigned int e=0; e<MAX_EXTRUDERS; e++)
        totalFilament[e] = 0.0;
    
    currentSpeed = 0;
    retractionSpeed = 45;
    isRetracted = false;
    setFlavor(GCODE_FLAVOR_REPRAP);
    memset(extruderOffset, 0, sizeof(extruderOffset));
    f = stdout;
    
    firstLineIsRunOnce = false;
    firstLineSection = 0.0;

    extruder0Offset_X = 0;
    extruder0Offset_Y = 0;

    is2In1OutNozzle = false;
    ColorMixing = 0;
    ColorA = 0.0;
    ColorB = 0.0;
    OverlapCount = 0.0;
    ColorMixType = 0;
    FixedProportionColorA = 100;
    FixedProportionColorB = 0;
    
    currentLayer = -1;
    totalLayer = -1;

    memset(gcodeStr,0,sizeof(gcodeStr)/sizeof(char));//clear buffer
}

GCodeExport::~GCodeExport()
{
    if (f && f != stdout)
        fclose(f);
}

void GCodeExport::replaceTagInStart(const char* tag, const char* replaceValue)
{
    if (f == stdout)
    {
        cura::log("Replace:%s:%s\n", tag, replaceValue);
        return;
    }
    fpos_t oldPos;
    fgetpos(f, &oldPos);
    
    char buffer[1024];
    fseek(f, 0, SEEK_SET);
    fread(buffer, 1024, 1, f);
    
    char* c = strstr(buffer, tag);
    memset(c, ' ', strlen(tag));
    if (c) memcpy(c, replaceValue, strlen(replaceValue));
    
    fseek(f, 0, SEEK_SET);
    fwrite(buffer, 1024, 1, f);
    
    fsetpos(f, &oldPos);
}

void GCodeExport::setExtruderOffset(int id, Point p)
{
    extruderOffset[id] = p;
}

void GCodeExport::setSwitchExtruderCode(std::string preSwitchExtruderCode, std::string postSwitchExtruderCode)
{
    this->preSwitchExtruderCode = preSwitchExtruderCode;
    this->postSwitchExtruderCode = postSwitchExtruderCode;
}

void GCodeExport::setFlavor(int flavor)
{
    this->flavor = flavor;
    if (flavor == GCODE_FLAVOR_MACH3)
    {
        for(int n=0; n<MAX_EXTRUDERS; n++)
            extruderCharacter[n] = 'A' + n;
    } else {
        for(int n=0; n<MAX_EXTRUDERS; n++)
        {
            if(n == 1)
            {
                extruderCharacter[n] = 'B';
            } else {
                extruderCharacter[n] = 'E';
            }
        }
    }
}
int GCodeExport::getFlavor()
{
    return this->flavor;
}

void GCodeExport::setFilename(const char* filename)
{
    f = fopen(filename, "w+");
}

bool GCodeExport::isOpened()
{
    return f != nullptr;
}

void GCodeExport::setExtrusion(int layerThickness, int filamentDiameter, int flow)
{
    double filamentArea = M_PI * (INT2MM(filamentDiameter) / 2.0) * (INT2MM(filamentDiameter) / 2.0);
    if (flavor == GCODE_FLAVOR_ULTIGCODE || flavor == GCODE_FLAVOR_REPRAP_VOLUMATRIC)//UltiGCode uses volume extrusion as E value, and thus does not need the filamentArea in the mix.
        extrusionPerMM = INT2MM(layerThickness);
    else
        extrusionPerMM = INT2MM(layerThickness) / filamentArea * double(flow) / 100.0;
}

void GCodeExport::setRetractionSettings(int retractionAmount, int retractionSpeed, int extruderSwitchRetraction, int minimalExtrusionBeforeRetraction, int zHop, int retractionAmountPrime)
{
    this->retractionAmount = INT2MM(retractionAmount);
    this->retractionAmountPrime = INT2MM(retractionAmountPrime);
    this->retractionSpeed = retractionSpeed;
    this->extruderSwitchRetraction = INT2MM(extruderSwitchRetraction);
    this->minimalExtrusionBeforeRetraction = INT2MM(minimalExtrusionBeforeRetraction);
    this->retractionZHop = zHop;
}

void GCodeExport::setZ(int z)
{
    this->zPos = z;
}

void GCodeExport::setCurrentLayer(int _currentLayer)
{
    this->currentLayer = _currentLayer;
}

void GCodeExport::setTotalLayer(int _totalLayer)
{
    this->totalLayer = _totalLayer;
}

Point GCodeExport::getPositionXY()
{
    return Point(currentPosition.x, currentPosition.y);
}

void GCodeExport::resetStartPosition()
{
    startPosition.x = INT32_MIN;
    startPosition.y = INT32_MIN;
}

Point GCodeExport::getStartPositionXY()
{
    return Point(startPosition.x, startPosition.y);
}

int GCodeExport::getPositionZ()
{
    return currentPosition.z;
}

int GCodeExport::getExtruderNr()
{
    return extruderNr;
}

double GCodeExport::getTotalFilamentUsed(int e)
{
    if (e == extruderNr)
        return totalFilament[e] + extrusionAmount;
    return totalFilament[e];
}

double GCodeExport::getTotalPrintTime()
{
    return totalPrintTime;
}

void GCodeExport::updateTotalPrintTime()
{
    totalPrintTime += estimateCalculator.calculate();
    estimateCalculator.reset();
}

void GCodeExport::writeComment(const char* comment, ...)
{
    va_list args;
    va_start(args, comment);
    fprintf(f, ";");
    vfprintf(f, comment, args);
    if (flavor == GCODE_FLAVOR_BFB)
        fprintf(f, "\r\n");
    else
        fprintf(f, "\n");
    va_end(args);
}

char GCodeExport::getEncryption(char source, int key, int pos)
{
    if((source >= 'a' && source <= 'z'))
    {
        source = source + (key + pos) % 26;
        if(source > 'z')
        {
            source -= 26;
        }
    }else if((source >= 'A' && source <= 'Z'))
    {
        source = source + (key + pos) % 26;
        if(source > 'Z')
        {
            source -= 26;
        }
    }else if((source >= '0' && source <= '9'))
    {
        source = source + (key + pos) % 10;
        if(source > '9')
        {
            source -= 10;
        }
    }
    return source;
}

void GCodeExport::writeLine(const char* line, ...)
{
    char gcode_cmd[GCODE_MAX_LENGTH];
    unsigned int checksum = 0;
    va_list args;
    va_start(args, line);
    //vfprintf(f, line, args);
    vsprintf(gcode_cmd, line, args);
    if(is2In1OutNozzle)
    {
        int len = strlen(gcode_cmd);
        for(int i = 0;i < len;i ++)
        {
            gcode_cmd[i] = getEncryption(gcode_cmd[i],20,i);
        }
    }
    for(int i = 0; i < GCODE_MAX_LENGTH; i++)
    {
        if(gcode_cmd[i] == '\0')
        {
            break;
        }
        checksum ^= gcode_cmd[i];
    }
    fprintf(f, "%s $%d", gcode_cmd, checksum);
    if (flavor == GCODE_FLAVOR_BFB)
        fprintf(f, "\r\n");
    else
        fprintf(f, "\n");
    va_end(args);
}

void GCodeExport::resetExtrusionValue()
{
    if (extrusionAmount != 0.0 && flavor != GCODE_FLAVOR_MAKERBOT && flavor != GCODE_FLAVOR_BFB)
    {
        if(is2In1OutNozzle)
        {
            writeLine("G92 E0 B0");
            extrusionAAmount = 0.0;
            extrusionBAmount = 0.0;
        } else
        {
            writeLine("G92 %c0", extruderCharacter[extruderNr]);
        }
        totalFilament[extruderNr] += extrusionAmount;
        extrusionAmountAtPreviousRetraction -= extrusionAmount;
        extrusionAmount = 0.0;
        extrusionAmountTmp = 0.0;
    }
}

void GCodeExport::writeDelay(double timeAmount)
{
    writeLine("G4 P%d", int(timeAmount * 1000));
    totalPrintTime += timeAmount;
}

// writeMove for GCODE_FLAVOR_BFB
void GCodeExport::writeMove4GFB(Point p, int speed, int lineWidth)
{
    //For Bits From Bytes machines, we need to handle this completely differently. As they do not use E values but RPM values.
    float fspeed = speed * 60;
    float rpm = (extrusionPerMM * double(lineWidth) / 1000.0) * speed * 60;
    const float mm_per_rpm = 4.0; //All BFB machines have 4mm per RPM extrusion.
    rpm /= mm_per_rpm;
    if (rpm > 0)
    {
        if (isRetracted)
        {
            if (currentSpeed != int(rpm * 10))
            {
                //writeComment("; %f e-per-mm %d mm-width %d mm/s", extrusionPerMM, lineWidth, speed);
                writeLine("M108 S%0.1f", rpm);
                currentSpeed = int(rpm * 10);
            }
            writeLine("M%d01", extruderNr + 1);
            isRetracted = false;
        }
        //Fix the speed by the actual RPM we are asking, because of rounding errors we cannot get all RPM values, but we have a lot more resolution in the feedrate value.
        // (Trick copied from KISSlicer, thanks Jonathan)
        fspeed *= (rpm / (roundf(rpm * 100) / 100));

        //Increase the extrusion amount to calculate the amount of filament used.
        Point diff = p - getPositionXY();
        extrusionAmount += extrusionPerMM * INT2MM(lineWidth) * vSizeMM(diff);
    }else{
        //If we are not extruding, check if we still need to disable the extruder. This causes a retraction due to auto-retraction.
        if (!isRetracted)
        {
            writeLine("M103");
            isRetracted = true;
        }
    }
    writeLine("G1 X%0.3f Y%0.3f Z%0.3f F%0.1f", INT2MM(p.X - extruderOffset[extruderNr].X - extruder0Offset_X), INT2MM(p.Y - extruderOffset[extruderNr].Y - extruder0Offset_Y), INT2MM(zPos), fspeed);
}

void GCodeExport::writeMoveE4TIOO_LAYER()
{
    if(totalLayer == 0)
    {
        return;
    }
    int count = currentLayer * OverlapCount / totalLayer + 0.5;
    count = count  % 2;
    if(count == 0)
    {
        extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
        extrusionAAmount += extrusionAmountTmp * 1;
        extrusionBAmount += 0;
        snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
    }else
    {
        extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
        extrusionBAmount += extrusionAmountTmp * 1;
        extrusionAAmount += 0;
        snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
    }
}

void GCodeExport::writeMoveE4TIOO_MIX()
{
    if(totalLayer == 0)
    {
        return;
    }
    int layer_height_percent = currentLayer * 100 / totalLayer;
    if(1 == ColorMixType)
    {
        extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
        extrusionAAmount += extrusionAmountTmp * FixedProportionColorA/100.0f;
        extrusionBAmount += extrusionAmountTmp * (1.0f - FixedProportionColorA/100.0f);
        snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
    }
    else if(0 == ColorMixType){
        if (ColorA > ColorB)
        {
            if(layer_height_percent >= ColorB && layer_height_percent <= ColorA)
            {
                float percent = (layer_height_percent - ColorB)/(ColorA - ColorB) ;
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * percent;
                extrusionBAmount += extrusionAmountTmp * (1 - percent);
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }else if(layer_height_percent < ColorB)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 0;
                extrusionBAmount += extrusionAmountTmp * 1;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }else if(layer_height_percent > ColorA)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 1;
                extrusionBAmount += extrusionAmountTmp * 0;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }
        }else if (ColorA < ColorB)
        {
            if(layer_height_percent <= ColorB && layer_height_percent >= ColorA)
            {
                float percent = (layer_height_percent - ColorA)/(ColorB - ColorA) ;
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * (1 - percent);
                extrusionBAmount += extrusionAmountTmp * percent;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }else if(layer_height_percent < ColorA)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 1;
                extrusionBAmount += extrusionAmountTmp * 0;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }else if(layer_height_percent > ColorB)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 0;
                extrusionBAmount += extrusionAmountTmp * 1;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }
        }else if (ColorA == ColorB)
        {
            if(layer_height_percent < ColorA)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 1;
                extrusionBAmount += extrusionAmountTmp * 0;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }else if(layer_height_percent > ColorA)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 0;
                extrusionBAmount += extrusionAmountTmp * 1;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }else if(layer_height_percent == ColorA)
            {
                extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
                extrusionAAmount += extrusionAmountTmp * 0.5;
                extrusionBAmount += extrusionAmountTmp * 0.5;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
            }
        }
    }
}

// writeMove EB for 2 in 1 out nozzle
void GCodeExport::writeMoveE4TIOO()
{
    if(currentLayer == -1)
    {
        extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
        extrusionAAmount += extrusionAmountTmp * 0.5;
        extrusionBAmount += extrusionAmountTmp * 0.5;
        snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, extrusionAAmount, extrusionBAmount);
    }else if(currentLayer >= 0)
    {
        if(ColorMixing == COLOR_LAYER)
        {
            writeMoveE4TIOO_LAYER();
        }
        else if (ColorMixing == COLOR_MIX)
        {
            writeMoveE4TIOO_MIX();
        }else if (ColorMixing == COLOR_DOUBLE)
        {
            extrusionAmountTmp = extrusionAmount - extrusionAAmount - extrusionBAmount;
            if(extruderNr == 0)
            {
                extrusionAAmount += extrusionAmountTmp;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f", gcodeStr, extrusionAAmount);
            }
            else if(extruderNr == 1)
            {
                extrusionBAmount += extrusionAmountTmp;
                snprintf(gcodeStr, sizeof(gcodeStr), "%s B%0.5f", gcodeStr, extrusionBAmount);
            }
        }else if (ColorMixing == COLOR_SINGLE)
        {
            snprintf(gcodeStr, sizeof(gcodeStr), "%s E%0.5f B%0.5f", gcodeStr, 0.5*extrusionAmount, 0.5*extrusionAmount);
        }
    }
}

// writeMove EB for first line
// Print some silk from the origin point to the starting point,then reset.
void GCodeExport::writeMoveE4FL(Point p)
{
    static bool is_run_once = false;
    if(!is_run_once)
    {
        double x = INT2MM(p.X - extruderOffset[extruderNr].X);
        double y = INT2MM(p.Y - extruderOffset[extruderNr].Y);
        double diff = sqrt(x*x+y*y);
        double e = 2.0 * this->firstLineSection * diff;
        if(e <= 0.0)
        {
            e = 10.0f;
        }
        if(is2In1OutNozzle)
        {
            writeLine("%s E%0.5f B%0.5f",gcodeStr, e * 0.5, e * 0.5);
            writeLine("G92 E0 B0");
        } else {
            writeLine("%s %c%0.5f", gcodeStr, extruderCharacter[extruderNr], e);
            writeLine("G92 %c0", extruderCharacter[extruderNr]);
        }
        is_run_once = true;
    }
}

void GCodeExport::writeMove(Point p, int speed, int lineWidth)
{
    memset(gcodeStr,0,sizeof(gcodeStr)/sizeof(char));//clear buffer

    if (currentPosition.x == p.X && currentPosition.y == p.Y && currentPosition.z == zPos)
        return;

    if (flavor == GCODE_FLAVOR_BFB)
    {
        writeMove4GFB(p, speed, lineWidth);
    }else{
        
        //Normal E handling.
        if (lineWidth != 0)
        {
            Point diff = p - getPositionXY();
            if (isRetracted)
            {
                if (retractionZHop > 0)
                    writeLine("G1 Z%0.3f", float(currentPosition.z)/1000);
                if (flavor == GCODE_FLAVOR_ULTIGCODE || flavor == GCODE_FLAVOR_REPRAP_VOLUMATRIC)
                {
                    writeLine("G11");
                }else{
                    extrusionAmount += retractionAmountPrime;
                    if(is2In1OutNozzle)
                    {
                        if(/* layerNr >= 0 && */COLOR_SINGLE == ColorMixing)
                            writeLine("G1 F%i E%0.5f B%0.5f", retractionSpeed * 60, 0.5* extrusionAmount, 0.5* extrusionAmount);
                        else
                            writeLine("G1 F%i E%0.5f B%0.5f", retractionSpeed * 60, extrusionAAmount, extrusionBAmount);
                    }
                    else
                    {
                        writeLine("G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[extruderNr], extrusionAmount);
                    }
                    currentSpeed = retractionSpeed;
                    estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), extrusionAmount), currentSpeed);
                }
                if (extrusionAmount > 10000.0) //According to https://github.com/Ultimaker/CuraEngine/issues/14 having more then 21m of extrusion causes inaccuracies. So reset it every 10m, just to be sure.
                    resetExtrusionValue();
                isRetracted = false;
            }
            extrusionAmount += extrusionPerMM * INT2MM(lineWidth) * vSizeMM(diff);
            snprintf(gcodeStr, sizeof(gcodeStr), "G1");
        }else{
            snprintf(gcodeStr, sizeof(gcodeStr), "G0");
        }

        if (currentSpeed != speed)
        {
            snprintf(gcodeStr, sizeof(gcodeStr), "%s F%i", gcodeStr, speed * 60);
            currentSpeed = speed;
        }

        snprintf(gcodeStr, sizeof(gcodeStr), "%s X%0.3f Y%0.3f", gcodeStr, INT2MM(p.X - extruderOffset[extruderNr].X - extruder0Offset_X), INT2MM(p.Y - extruderOffset[extruderNr].Y - extruder0Offset_Y));
        if (zPos != currentPosition.z)
        {
            snprintf(gcodeStr, sizeof(gcodeStr), "%s Z%0.3f", gcodeStr, INT2MM(zPos));
        }
        if (lineWidth != 0)
        {
            if(!is2In1OutNozzle)
            {
                snprintf(gcodeStr, sizeof(gcodeStr), "%s %c%0.5f", gcodeStr, extruderCharacter[extruderNr], extrusionAmount);
            }else
            {
                writeMoveE4TIOO();
            }
        }
    }
    if(!firstLineIsRunOnce)
    {
        writeMoveE4FL(p);
        firstLineIsRunOnce = true;
    } else {
        writeLine(gcodeStr);
    }

    currentPosition = Point3(p.X, p.Y, zPos);
    startPosition = currentPosition;
    estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), extrusionAmount), speed);
}

void GCodeExport::writeRetraction(bool force)
{
    if (flavor == GCODE_FLAVOR_BFB)//BitsFromBytes does automatic retraction.
        return;
    
    if (retractionAmount > 0 && !isRetracted && (extrusionAmountAtPreviousRetraction + minimalExtrusionBeforeRetraction < extrusionAmount || force))
    {
        if (flavor == GCODE_FLAVOR_ULTIGCODE || flavor == GCODE_FLAVOR_REPRAP_VOLUMATRIC)
        {
            writeLine("G10");
        }else{
            if(is2In1OutNozzle)
            {
                if(ColorMixing == COLOR_DOUBLE)
                {
                    if(extruderNr == 0)
                        writeLine("G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[extruderNr], extrusionAAmount - retractionAmount);
                    else if(extruderNr == 1)
                        writeLine("G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[extruderNr], extrusionBAmount - retractionAmount);
                }else
                {
                    if (currentLayer >= 0 && ColorMixing == COLOR_SINGLE)
                    {
                        writeLine("G1 F%i E%0.5f B%0.5f", retractionSpeed * 60,  0.5*extrusionAmount - retractionAmount, 0.5*extrusionAmount);
                    }else
                    {
                        extrusionAmountTmp = retractionAmount * 0.5;
                        writeLine("G1 F%i E%0.5f B%0.5f", retractionSpeed * 60, extrusionAAmount - extrusionAmountTmp, extrusionBAmount - extrusionAmountTmp);
                    }
                }
            }else
            {
                writeLine("G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[extruderNr], extrusionAmount - retractionAmount);
            }
            currentSpeed = retractionSpeed;
            estimateCalculator.plan(TimeEstimateCalculator::Position(INT2MM(currentPosition.x), INT2MM(currentPosition.y), INT2MM(currentPosition.z), extrusionAmount - retractionAmount), currentSpeed);
        }
        if (retractionZHop > 0)
            writeLine("G1 Z%0.3f", INT2MM(currentPosition.z + retractionZHop));
        extrusionAmountAtPreviousRetraction = extrusionAmount;
        isRetracted = true;
    }
}

void GCodeExport::switchExtruder(int newExtruder)
{
    if (extruderNr == newExtruder)
        return;
    if (flavor == GCODE_FLAVOR_BFB)
    {
        if (!isRetracted)
            writeLine("M103");
        isRetracted = true;
        return;
    }
    
    resetExtrusionValue();
    if (flavor == GCODE_FLAVOR_ULTIGCODE || flavor == GCODE_FLAVOR_REPRAP_VOLUMATRIC)
    {
        writeLine("G10 S1");
    }else{
        if(is2In1OutNozzle)
        {
            /*
            snprintf(gcodeStr, sizeof(gcodeStr), "G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[extruderNr], extrusionAmount - extruderSwitchRetraction);
            writeChecksum(gcodeStr);
            memset(gcodeStr,0,sizeof(gcodeStr)/sizeof(char));
            extrusionAmount += extruderSwitchRetraction;
            if((extruderNr+1)%2 == 0)
            {
                extrusionAAmount = extrusionAmount;
                extrusionBAmount = 0;
            }else{
                extrusionAAmount = 0;
                extrusionBAmount = extrusionAmount;
            }
            snprintf(gcodeStr, sizeof(gcodeStr), "G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[(extruderNr+1)%2], extrusionAmount);
            writeChecksum(gcodeStr);
            snprintf(gcodeStr, sizeof(gcodeStr), "G92 E0 B0");
            */
        }else
        {
            writeLine("G1 F%i %c%0.5f", retractionSpeed * 60, extruderCharacter[extruderNr], extrusionAmount - extruderSwitchRetraction);
        }
        currentSpeed = retractionSpeed;
    }
    if (retractionZHop > 0)
        writeLine("G1 Z%0.3f", INT2MM(currentPosition.z + retractionZHop));
    extruderNr = newExtruder;
    if (flavor == GCODE_FLAVOR_MACH3)
        resetExtrusionValue();
    isRetracted = true;
    writeCode(preSwitchExtruderCode.c_str());
    if (flavor == GCODE_FLAVOR_MAKERBOT)
        writeLine("M135 T%i", extruderNr);
    else
        writeLine("T%i", extruderNr);
    writeCode(postSwitchExtruderCode.c_str());
}

void GCodeExport::writeCode(const char* str)
{
    fprintf(f, "%s", str);
    if (flavor == GCODE_FLAVOR_BFB)
        fprintf(f, "\r\n");
    else
        fprintf(f, "\n");
}

void GCodeExport::writeFanCommand(int speed)
{
    if (currentFanSpeed == speed)
        return;
    if (speed > 0)
    {
        if (flavor == GCODE_FLAVOR_MAKERBOT)
            writeLine("M126 T0 ; value = %d", speed * 255 / 100);
        else if (flavor == GCODE_FLAVOR_MACH3)
            writeLine("M106 P%d", speed * 255 / 100);
        else
            writeLine("M106 S%d", speed * 255 / 100);
    }
    else
    {
        if (flavor == GCODE_FLAVOR_MAKERBOT)
            writeLine("M127 T0");
        else if (flavor == GCODE_FLAVOR_MACH3)
            writeLine("M106 P%d", 0);
        else
            writeLine("M107");
    }
    currentFanSpeed = speed;
}

int GCodeExport::getFileSize(){
    return ftell(f);
}
void GCodeExport::tellFileSize() {
    float fsize = ftell(f);
    if(fsize > 1024*1024) {
        fsize /= 1024.0*1024.0;
        cura::log("Wrote %5.1f MB.\n",fsize);
    }
    if(fsize > 1024) {
        fsize /= 1024.0;
        cura::log("Wrote %5.1f kilobytes.\n",fsize);
    }
}

void GCodeExport::finalize(int maxObjectHeight, int moveSpeed, const char* endCode)
{
    writeFanCommand(0);
    writeRetraction();
    setZ(maxObjectHeight + 5000);
    writeMove(getPositionXY(), moveSpeed, 0);
    writeCode(endCode);
    cura::log("Print time: %d\n", int(getTotalPrintTime()));
    cura::log("Filament: %d\n", int(getTotalFilamentUsed(0)));
    cura::log("Filament2: %d\n", int(getTotalFilamentUsed(1)));
    
    if (getFlavor() == GCODE_FLAVOR_ULTIGCODE)
    {
        char numberString[16];
        sprintf(numberString, "%d", int(getTotalPrintTime()));
        replaceTagInStart("<__TIME__>", numberString);
        sprintf(numberString, "%d", int(getTotalFilamentUsed(0)));
        replaceTagInStart("<FILAMENT>", numberString);
        sprintf(numberString, "%d", int(getTotalFilamentUsed(1)));
        replaceTagInStart("<FILAMEN2>", numberString);
    }
}

void GCodeExport::setFirstLineSection(int initialLayerThickness, int filamentDiameter, int filamentFlow, int layer0extrusionWidth)
{
    double _filamentArea = M_PI * (INT2MM(filamentDiameter) / 2.0) * (INT2MM(filamentDiameter) / 2.0);
    if (flavor == GCODE_FLAVOR_ULTIGCODE || flavor == GCODE_FLAVOR_REPRAP_VOLUMATRIC)
        this->firstLineSection = INT2MM(initialLayerThickness) * INT2MM(layer0extrusionWidth);
    else
        this->firstLineSection = INT2MM(initialLayerThickness) / _filamentArea * double(filamentFlow) / 100.0 * INT2MM(layer0extrusionWidth);
}

void GCodeExport::setExtruder0OffsetXY(int _extruder0Offset_X, int _extruder0Offset_Y)
{
    extruder0Offset_X = _extruder0Offset_X;
    extruder0Offset_Y = _extruder0Offset_Y;
}

void GCodeExport::setColorMixing(int _ColorMixing, int _ColorA, int _ColorB, int _OverlapCount, int _ColorMixType, int _FixedProportionColorA, int _FixedProportionColorB, int _is2In1OutNozzle)
{
    ColorMixing = _ColorMixing;
    ColorA = _ColorA;
    ColorB = _ColorB;
    OverlapCount = _OverlapCount;
    ColorMixType = _ColorMixType;
    FixedProportionColorA = _FixedProportionColorA;
    FixedProportionColorB = _FixedProportionColorB;
    is2In1OutNozzle = _is2In1OutNozzle==1?true:false;
}

GCodePath* GCodePlanner::getLatestPathWithConfig(GCodePathConfig* config)
{
    if (paths.size() > 0 && paths[paths.size()-1].config == config && !paths[paths.size()-1].done)
        return &paths[paths.size()-1];
    paths.push_back(GCodePath());
    GCodePath* ret = &paths[paths.size()-1];
    ret->retract = false;
    ret->config = config;
    ret->extruder = currentExtruder;
    ret->done = false;
    return ret;
}
void GCodePlanner::forceNewPathStart()
{
    if (paths.size() > 0)
        paths[paths.size()-1].done = true;
}

GCodePlanner::GCodePlanner(GCodeExport& gcode, int travelSpeed, int retractionMinimalDistance)
    : gcode(gcode), travelConfig(travelSpeed, 0, "travel")
{
    lastPosition = gcode.getPositionXY();
    comb = nullptr;
    extrudeSpeedFactor = 100;
    travelSpeedFactor = 100;
    extraTime = 0.0;
    totalPrintTime = 0.0;
    forceRetraction = false;
    alwaysRetract = false;
    currentExtruder = gcode.getExtruderNr();
    this->retractionMinimalDistance = retractionMinimalDistance;
}
GCodePlanner::~GCodePlanner()
{
    if (comb)
        delete comb;
}

void GCodePlanner::addTravel(Point p)
{
    GCodePath* path = getLatestPathWithConfig(&travelConfig);
    if (forceRetraction)
    {
        if (!shorterThen(lastPosition - p, retractionMinimalDistance))
        {
            path->retract = true;
        }
        forceRetraction = false;
    }else if (comb != nullptr)
    {
        vector<Point> pointList;
        if (comb->calc(lastPosition, p, pointList))
        {
            for(unsigned int n=0; n<pointList.size(); n++)
            {
                path->points.push_back(pointList[n]);
            }
        }else{
            if (!shorterThen(lastPosition - p, retractionMinimalDistance))
                path->retract = true;
        }
    }else if (alwaysRetract)
    {
        if (!shorterThen(lastPosition - p, retractionMinimalDistance))
            path->retract = true;
    }
    path->points.push_back(p);
    lastPosition = p;
}

void GCodePlanner::addExtrusionMove(Point p, GCodePathConfig* config)
{
    getLatestPathWithConfig(config)->points.push_back(p);
    lastPosition = p;
}

void GCodePlanner::moveInsideCombBoundary(int distance)
{
    if (!comb || comb->inside(lastPosition)) return;
    Point p = lastPosition;
    if (comb->moveInside(&p, distance))
    {
        //Move inside again, so we move out of tight 90deg corners
        comb->moveInside(&p, distance);
        if (comb->inside(p))
        {
            addTravel(p);
            //Make sure the that any retraction happens after this move, not before it by starting a new move path.
            forceNewPathStart();
        }
    }
}

void GCodePlanner::addPolygon(PolygonRef polygon, int startIdx, GCodePathConfig* config)
{
    Point p0 = polygon[startIdx];
    addTravel(p0);
    for(unsigned int i=1; i<polygon.size(); i++)
    {
        Point p1 = polygon[(startIdx + i) % polygon.size()];
        addExtrusionMove(p1, config);
        p0 = p1;
    }
    if (polygon.size() > 2)
        addExtrusionMove(polygon[startIdx], config);
}

void GCodePlanner::addPolygonsByOptimizer(Polygons& polygons, GCodePathConfig* config)
{
    Point tmpPoint = lastPosition;
    // Reset skin layer print order
    if(strcmp(config->name,"SKIN") == 0)
    {
        if(polygons.size() > 0 && polygons[polygons.size()-1].size() > 0)
            tmpPoint = polygons[0][0];
    }
    PathOrderOptimizer orderOptimizer(tmpPoint);
    //PathOrderOptimizer orderOptimizer(lastPosition);
    for(unsigned int i=0;i<polygons.size();i++)
        orderOptimizer.addPolygon(polygons[i]);
    orderOptimizer.optimize();
    for(unsigned int i=0;i<orderOptimizer.polyOrder.size();i++)
    {
        int nr = orderOptimizer.polyOrder[i];
        addPolygon(polygons[nr], orderOptimizer.polyStart[nr], config);
    }
}

void GCodePlanner::forceMinimalLayerTime(double minTime, int minimalSpeed)
{
    Point p0 = gcode.getPositionXY();
    double travelTime = 0.0;
    double extrudeTime = 0.0;
    for(unsigned int n=0; n<paths.size(); n++)
    {
        GCodePath* path = &paths[n];
        for(unsigned int i=0; i<path->points.size(); i++)
        {
            double thisTime = vSizeMM(p0 - path->points[i]) / double(path->config->speed);
            if (path->config->lineWidth != 0)
                extrudeTime += thisTime;
            else
                travelTime += thisTime;
            p0 = path->points[i];
        }
    }
    double totalTime = extrudeTime + travelTime;
    if (totalTime < minTime && extrudeTime > 0.0)
    {
        double minExtrudeTime = minTime - travelTime;
        if (minExtrudeTime < 1)
            minExtrudeTime = 1;
        double factor = extrudeTime / minExtrudeTime;
        for(unsigned int n=0; n<paths.size(); n++)
        {
            GCodePath* path = &paths[n];
            if (path->config->lineWidth == 0)
                continue;
            int speed = path->config->speed * factor;
            if (speed < minimalSpeed)
                factor = double(minimalSpeed) / double(path->config->speed);
        }
        
        //Only slow down with the minimal time if that will be slower then a factor already set. First layer slowdown also sets the speed factor.
        if (factor * 100 < getExtrudeSpeedFactor())
            setExtrudeSpeedFactor(factor * 100);
        else
            factor = getExtrudeSpeedFactor() / 100.0;
        
        if (minTime - (extrudeTime / factor) - travelTime > 0.1)
        {
            //TODO: Use up this extra time (circle around the print?)
            this->extraTime = minTime - (extrudeTime / factor) - travelTime;
        }
        this->totalPrintTime = (extrudeTime / factor) + travelTime;
    }else{
        this->totalPrintTime = totalTime;
    }
}

void GCodePlanner::writeGCode(bool liftHeadIfNeeded, int layerThickness)
{
    GCodePathConfig* lastConfig = nullptr;
    int extruder = gcode.getExtruderNr();

    for(unsigned int n=0; n<paths.size(); n++)
    {
        GCodePath* path = &paths[n];
        if (extruder != path->extruder)
        {
            extruder = path->extruder;
            gcode.switchExtruder(extruder);
        }else if (path->retract)
        {
            gcode.writeRetraction();
        }
        if (path->config != &travelConfig && lastConfig != path->config)
        {
            gcode.writeComment("TYPE:%s", path->config->name);
            lastConfig = path->config;
        }
        int speed = path->config->speed;
        
        if (path->config->lineWidth != 0)// Only apply the extrudeSpeedFactor to extrusion moves
            speed = speed * extrudeSpeedFactor / 100;
        else
            speed = speed * travelSpeedFactor / 100;
        
        if (path->points.size() == 1 && path->config != &travelConfig && shorterThen(gcode.getPositionXY() - path->points[0], path->config->lineWidth * 2))
        {
            //Check for lots of small moves and combine them into one large line
            Point p0 = path->points[0];
            unsigned int i = n + 1;
            while(i < paths.size() && paths[i].points.size() == 1 && shorterThen(p0 - paths[i].points[0], path->config->lineWidth * 2))
            {
                p0 = paths[i].points[0];
                i ++;
            }
            if (paths[i-1].config == &travelConfig)
                i --;
            if (i > n + 2)
            {
                p0 = gcode.getPositionXY();
                for(unsigned int x=n; x<i-1; x+=2)
                {
                    int64_t oldLen = vSize(p0 - paths[x].points[0]);
                    Point newPoint = (paths[x].points[0] + paths[x+1].points[0]) / 2;
                    int64_t newLen = vSize(gcode.getPositionXY() - newPoint);
                    if (newLen > 0)
                        gcode.writeMove(newPoint, speed, path->config->lineWidth * oldLen / newLen);
                    
                    p0 = paths[x+1].points[0];
                }
                gcode.writeMove(paths[i-1].points[0], speed, path->config->lineWidth);
                n = i - 1;
                continue;
            }
        }
        
        bool spiralize = path->config->spiralize;
        if (spiralize)
        {
            //Check if we are the last spiralize path in the list, if not, do not spiralize.
            for(unsigned int m=n+1; m<paths.size(); m++)
            {
                if (paths[m].config->spiralize)
                    spiralize = false;
            }
        }
        if (spiralize)
        {
            //If we need to spiralize then raise the head slowly by 1 layer as this path progresses.
            float totalLength = 0.0;
            int z = gcode.getPositionZ();
            Point p0 = gcode.getPositionXY();
            for(unsigned int i=0; i<path->points.size(); i++)
            {
                Point p1 = path->points[i];
                totalLength += vSizeMM(p0 - p1);
                p0 = p1;
            }
            
            float length = 0.0;
            p0 = gcode.getPositionXY();
            for(unsigned int i=0; i<path->points.size(); i++)
            {
                Point p1 = path->points[i];
                length += vSizeMM(p0 - p1);
                p0 = p1;
                gcode.setZ(z + layerThickness * length / totalLength);
                gcode.writeMove(path->points[i], speed, path->config->lineWidth);
            }
        }else{
            for(unsigned int i=0; i<path->points.size(); i++)
            {
                gcode.writeMove(path->points[i], speed, path->config->lineWidth);
            }
        }
    }
    
    gcode.updateTotalPrintTime();
    if (liftHeadIfNeeded && extraTime > 0.0)
    {
        gcode.writeComment("Small layer, adding delay of %f", extraTime);
        gcode.writeRetraction(true);
        gcode.setZ(gcode.getPositionZ() + MM2INT(3.0));
        gcode.writeMove(gcode.getPositionXY(), travelConfig.speed, 0);
        gcode.writeMove(gcode.getPositionXY() - Point(-MM2INT(20.0), 0), travelConfig.speed, 0);
        gcode.writeDelay(extraTime);
    }
}

}//namespace cura
