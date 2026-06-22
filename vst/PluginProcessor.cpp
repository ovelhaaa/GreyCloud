#include "PluginProcessor.h"
#include "PluginEditor.h"

CloudGreyVerbEditor::CloudGreyVerbEditor (CloudGreyVerbProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    addDSPControl("mix", "Mix");
    addDSPControl("texture", "Texture");
    addDSPControl("freeze", "Freeze");
    addDSPControl("feedback", "Feedback");
    addDSPControl("size", "Size");
    addDSPControl("diffusion", "Diffusion");
    addDSPControl("modDepth", "Mod Depth");
    addDSPControl("modRate", "Mod Rate");
    addDSPControl("damping", "Damping");
    addDSPControl("lowDamping", "Low Damp");
    addDSPControl("tone", "Tone");
    addDSPControl("shimmer", "Shimmer");
    addDSPControl("preDelay", "Pre-Delay");
    addDSPControl("stereoWidth", "Stereo Width");
    addDSPControl("inputGain", "Input");
    addDSPControl("outputGain", "Output");

    importButton.setButtonText("Load WASM JSON Preset...");
    importButton.onClick = [this] { loadJSONPreset(); };
    addAndMakeVisible(importButton);

    exportButton.setButtonText("Export to JSON...");
    exportButton.onClick = [this] { exportJSONPreset(); };
    addAndMakeVisible(exportButton);

    // Dimensions: 4 columns x 4 rows + footer
    setSize (600, 500);
}

CloudGreyVerbEditor::~CloudGreyVerbEditor()
{
}

void CloudGreyVerbEditor::addDSPControl(const juce::String& paramID, const juce::String& name) {
    auto wrapper = std::make_unique<PSlider>();
    
    wrapper->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wrapper->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
    addAndMakeVisible(wrapper->slider);

    wrapper->label.setText(name, juce::dontSendNotification);
    wrapper->label.setJustificationType(juce::Justification::centred);
    wrapper->label.attachToComponent(&wrapper->slider, false);
    addAndMakeVisible(wrapper->label);

    wrapper->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getVTS(), paramID, wrapper->slider);

    sliders.push_back(std::move(wrapper));
}

void CloudGreyVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour(24, 24, 28));

    g.setColour (juce::Colours::white);
    g.setFont (24.0f);
    g.drawFittedText ("Nimbus Reverb", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);
}

void CloudGreyVerbEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    bounds.removeFromTop(30); // header spacing
    
    auto footer = bounds.removeFromBottom(40);
    importButton.setBounds(footer.removeFromLeft(200).withSizeKeepingCentre(180, 30));
    exportButton.setBounds(footer.removeFromRight(200).withSizeKeepingCentre(180, 30));
    
    int numCols = 4;
    int numRows = 4;
    
    int w = bounds.getWidth() / numCols;
    int h = bounds.getHeight() / numRows;

    for (int i = 0; i < (int)sliders.size(); ++i) {
        int r = i / numCols;
        int c = i % numCols;
        
        auto cell = bounds.withTrimmedLeft(c * w).withTrimmedTop(r * h).withWidth(w).withHeight(h);
        // Leave room for label that was attached at the top natively by juce
        sliders[i]->slider.setBounds(cell.reduced(10).withTrimmedTop(15));
    }
}

void CloudGreyVerbEditor::exportJSONPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save GreyCloud Preset JSON",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("vst_preset.json"),
        "*.json");
        
    auto folderChooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(folderChooserFlags, [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file.isDirectory() || file.getFileName().isEmpty()) return;
        
        juce::DynamicObject::Ptr presetObj = new juce::DynamicObject();
        presetObj->setProperty("name", "VST Export");
        
        juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
        auto* vts = &audioProcessor.getVTS();
        
        auto state = vts->copyState();
        for (auto child : state)
        {
            if (child.hasType("PARAM"))
            {
                auto id = child.getProperty("id").toString();
                double val = static_cast<double>(child.getProperty("value"));
                paramsObj->setProperty(id, val);
            }
        }
        
        presetObj->setProperty("params", juce::var(paramsObj.get()));
        
        juce::Array<juce::var> presetsArray;
        presetsArray.add(juce::var(presetObj.get()));
        
        juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
        rootObj->setProperty("app", "GreyCloud");
        rootObj->setProperty("version", 1);
        rootObj->setProperty("presets", juce::var(presetsArray));
        
        juce::FileOutputStream fos(file);
        if (fos.openedOk())
        {
            fos.setPosition(0);
            fos.truncate();
            juce::JSON::writeToStream(fos, juce::var(rootObj.get()));
        }
    });
}

void CloudGreyVerbEditor::loadJSONPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select GreyCloud Preset JSON",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        "*.json");
        
    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    fileChooser->launchAsync(folderChooserFlags, [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile()) return;
        
        juce::var jsonObject = juce::JSON::parse(file);
        if (!jsonObject.isObject()) return;
        
        auto* obj = jsonObject.getDynamicObject();
        if (!(obj && obj->hasProperty("presets") && jsonObject["presets"].isArray())) return;

        auto* presetsArray = jsonObject["presets"].getArray();
        if (!presetsArray || presetsArray->isEmpty()) return;

        juce::PopupMenu m;
        for (int i = 0; i < presetsArray->size(); ++i)
        {
            auto presetVar = presetsArray->getReference(i);
            if (presetVar.isObject() && presetVar.getDynamicObject()->hasProperty("name"))
            {
                m.addItem(i + 1, presetVar.getDynamicObject()->getProperty("name").toString());
            }
        }
        
        if (m.getNumItems() == 0) return;
        
        // Pass jsonObject by value so its ref-count keeps the tree alive
        m.showMenuAsync(juce::PopupMenu::Options(), [this, jsonObject] (int result)
        {
            if (result <= 0) return;
            int idx = result - 1;
            auto* pArray = jsonObject["presets"].getArray();
            if (!pArray) return;
            auto presetVar = pArray->getReference(idx);
            if (presetVar.isObject() && presetVar.getDynamicObject()->hasProperty("params"))
            {
                auto paramsVar = presetVar.getDynamicObject()->getProperty("params");
                if (paramsVar.isObject())
                {
                    auto* paramsObj = paramsVar.getDynamicObject();
                    auto* vts = &audioProcessor.getVTS();
                    for (auto& prop : paramsObj->getProperties())
                    {
                        auto id = prop.name.toString();
                        if (auto* param = vts->getParameter(id))
                        {
                            float normalized = param->convertTo0to1(static_cast<float>(static_cast<double>(prop.value)));
                            param->setValueNotifyingHost(normalized);
                        }
                    }
                }
            }
        });
    });
}

