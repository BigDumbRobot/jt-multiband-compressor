// Renders the plugin icon and a screenshot of the editor:
//   JTRender <icon.png> <screenshot.png> [preset index]
#include "Logo.h"
#include "PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::puts ("usage: JTRender icon.png screenshot.png [preset]");
        return 1;
    }

    auto save = [] (const juce::Image& img, const char* path)
    {
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (path));
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };

    juce::Image icon (juce::Image::ARGB, 1024, 1024, true);
    {
        juce::Graphics g (icon);
        theme::drawTransformerBot (g, { 0.0f, 0.0f, 1024.0f, 1024.0f }, true);
    }
    save (icon, argv[1]);

    MultibandProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    if (argc > 3)
        proc.setCurrentProgram (juce::String (argv[3]).getIntValue());
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    // Play a few seconds of pink-ish noise with a little tilt so the RTA and meters show something.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (7);
    float lp[2] {};
    for (int blk = 0; blk < 200; ++blk)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                lp[ch] = 0.97f * lp[ch] + 0.03f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (ch, i, 1.5f * lp[ch] + 0.05f * (rng.nextFloat() * 2.0f - 1.0f));
            }
        proc.processBlock (buf, midi);
        if (blk % 10 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), argv[2]);
    editor.reset();
    return 0;
}
