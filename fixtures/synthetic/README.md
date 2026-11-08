# Synthetic Eclipse comparison fixture

`eclipse_can_project` is a small, license-safe project for parser, path-discovery, and project-preservation smoke tests. It includes a DBC input, an illustrative ECUC definition/value ARXML pair, CDT project metadata, a SCons entry point, and representative owned/generated files.

This is **not** a customer project, a legacy Xtend baseline, or evidence of AUTOSAR XSD compliance. The ARXML files are well-formed examples only; they are not validated against a release-specific AUTOSAR schema or vendor ECUC definition. The project has not yet been opened in a particular Eclipse/CDT installation.

Real differential qualification still requires approved, frozen existing Eclipse projects and their exact generated ARXML/C/H, generator versions, BAT/SCons commands, toolchain settings, and build results. Keep those customer fixtures outside the public repository and reference them through controlled manifests.
