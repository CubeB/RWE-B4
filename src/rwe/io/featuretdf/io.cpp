#include "io.h"

namespace rwe
{
    FeatureTdf parseFeatureTdf(const TdfBlock& tdf)
    {
        FeatureTdf f;

        std::string emptyString;

        f.world = tdf.findValue("world").value_or(emptyString);
        f.description = tdf.findValue("description").value_or(emptyString);
        f.category = tdf.findValue("category").value_or(emptyString);

        f.footprintX = tdf.extractUint("footprintx").value_or(1);
        f.footprintZ = tdf.extractUint("footprintz").value_or(1);
        f.height = tdf.extractUint("height").value_or(0);

        f.animating = tdf.extractBool("animating").value_or(false);
        f.fileName = tdf.findValue("filename").value_or(emptyString);
        f.seqName = tdf.findValue("seqname").value_or(emptyString);
        f.animTrans = tdf.extractBool("animtrans").value_or(false);
        f.seqNameShad = tdf.findValue("seqnameshad").value_or(emptyString);
        f.shadTrans = tdf.extractBool("shadtrans").value_or(false);

        f.object = tdf.findValue("object").value_or(emptyString);

        f.reclaimable = tdf.extractBool("reclaimable").value_or(false);
        f.autoreclaimable = tdf.extractBool("autoreclaimable").value_or(true);
        f.seqNameReclamate = tdf.findValue("seqnamereclamate").value_or(emptyString);
        f.featureReclamate = tdf.findValue("featurereclamate").value_or(emptyString);
        f.metal = tdf.extractUint("metal").value_or(0);
        f.energy = tdf.extractUint("energy").value_or(0);

        f.flamable = tdf.extractBool("flamable").value_or(false);
        f.seqNameBurn = tdf.findValue("seqnameburn").value_or(emptyString);
        f.seqNameBurnShad = tdf.findValue("seqnameburnshad").value_or(emptyString);
        f.featureBurnt = tdf.findValue("featureburnt").value_or(emptyString);
        f.burnMin = tdf.extractUint("burnmin").value_or(0);
        f.burnMax = tdf.extractUint("burnmax").value_or(0);
        f.sparkTime = tdf.extractUint("sparktime").value_or(0);
        f.spreadChance = tdf.extractUint("spreadchance").value_or(0);
        f.burnWeapon = tdf.findValue("burnweapon").value_or(emptyString);

        f.geothermal = tdf.extractBool("geothermal").value_or(false);

        f.hitDensity = tdf.extractUint("hitdensity").value_or(0);

        f.reproduce = tdf.extractUint("reproduce").value_or(0);
        f.reproduceArea = tdf.extractUint("reproducearea").value_or(0);

        f.noDisplayInfo = tdf.extractBool("nodisplayinfo").value_or(false);

        f.permanent = tdf.extractBool("permanent").value_or(false);

        f.blocking = tdf.extractBool("blocking").value_or(false);

        f.indestructible = tdf.extractBool("indestructible").value_or(false);
        // The original's feature parser (0x422A20) reads every integer key
        // through one helper with a default it seeds to zero for the whole
        // block (xor esi,esi at 0x4226F4), so a feature that omits `damage`
        // has none: the first blast to reach it takes it. See TOTALA-EXE.md
        // §24, "What a blast does to a feature".
        f.damage = tdf.extractUint("damage").value_or(0);
        f.seqNameDie = tdf.findValue("seqnamedie").value_or(emptyString);
        f.featureDead = tdf.findValue("featuredead").value_or(emptyString);

        return f;
    }
}
