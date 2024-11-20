# Clean compiled schedulers
rm -R schedulers/build/*

# Clean copied to be path-findable from UPPAAL.
rm libcapacity.so libfixed.so librnd_choice.so

# Clean artefacts created by running
## Only keep instances.toml and model_configuration.toml files
find instances/ ! -name "instances.toml" -type f ! -name "model_configuration.toml" -type f -delete
find instances/ -type d -empty -delete

