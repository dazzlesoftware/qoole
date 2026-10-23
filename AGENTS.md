# Source organization and naming

- Follow the corresponding Unreal Engine module names and source layout for
  all new engine source. The local reference is
  `G:/GitHub/dazzlesoftware/UnrealEngine/Engine/Source` when available.
- Put feature code in its owning module and feature subfolders, with `Public`
  and `Private` boundaries. Do not accumulate unrelated asset editors, graph
  widgets or compilers in MapEditor or the GenesisEditor program.
- Runtime material types and translation belong under
  `Runtime/Engine/{Public,Private}/Materials`; material authoring belongs to
  `Editor/MaterialEditor`. Keep runtime code independent of editor modules.
- Application code should contain only host integration and callbacks for
  selection, transactions and other application-specific operations.
- Use established Unreal naming where it describes the implemented feature.
  Do not add empty modules just to mirror the reference tree. In particular,
  add MaterialShaderQualitySettings only when shader-quality settings exist.
- Do not introduce Kismet names for new scripting or gameplay graph code. That
  terminology will be handled separately; use descriptive feature names while
  preserving the same runtime/editor/compiler separation.
- Update project files, solution registration, includes and documentation when
  moving source. Preserve existing user work and serialized asset compatibility.
## Coding and audit requirements

- Use explicit types; do not introduce `auto`, including generic lambda parameters.
- Use Genesis API branding (`GENESIS_LOG`, `GENESIS_BUILD_DEBUG`); do not introduce Unreal product prefixes. Slate names and the corresponding module layout are retained.
- Put tests and test harness implementation under `Engine/Source/Programs`, separate from runtime and editor modules.
- Localize display strings through Core's internationalization service. Keep serialized keys, asset identities, paths, shader source and protocol tokens invariant.
- Use explicit error results for expected validation failures. Preserve exception barriers where foreign runtimes or external callbacks require them until their contracts are changed safely.
- Use the repository `.clang-format` for first-party C++ changes. Do not reformat vendored source.
- Give shared validation limits and configurable defaults meaningful names in their owning feature module.
