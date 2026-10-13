# D.C.4 MES opcode table v2

Operand signatures are **authoritative** (from VM fetcher sub_438790, verified 50707/50707 labels).
Call profiles from handler blocks in MesVM_ExecuteOpcode (sub_428640). '?' = guessed role.

| op | operands | handler | calls |
|---|---|---|---|
| 0x00 | nop (no advance) | 0x42e60e |  |
| 0x01 | u8, u8 | — | INVALID (default case) |
| 0x02 | u8, u8 | — | INVALID (default case) |
| 0x03 | u8, u8 | 0x42ee4f | Mes_GetLabelAddr, sub_403700, sub_438650, sub_421A00 |
| 0x04 | u8, u8 | 0x42ef4d | Text_TagReplace?, sub_403170 |
| 0x05 | u8, u8 | 0x42e286 | sub_43C4F0, VM_VarRead, sub_43B0F0, Audio_StopChannel, sub_43BA50 |
| 0x06 | u8, u8 | 0x42919d |  |
| 0x07 | u8, u8 | 0x42917c | ds |
| 0x08 | u8, u8 | — | INVALID (default case) |
| 0x09 | u8, u8 | 0x42998e | sub_418C50 |
| 0x0A | u8, u8 | 0x4299cc | sub_41FCD0, VM_VarRead, sub_414740, sub_440880, sub_442430, sub_419950, sub_4148B0, sub_419F30, sub_418F20, su |
| 0x0B | u8, u8 | 0x429923 | Snd_?, VM_AdvanceStream, Gfx_? |
| 0x0C | u8, u8 | 0x429fba | Snd_?, sub_435B70, sub_413360, VM_AdvanceStream |
| 0x0D | u8, u8 | — | INVALID (default case) |
| 0x0E | u8, u8 | — | INVALID (default case) |
| 0x0F | u8, u8 | 0x429ff8 | sub_425D20, sub_44BF60, sub_407820, sub_407810, sub_407780, VM_VarRead, sub_44A730, sub_4442C0, VM_VarWrite, s |
| 0x10 | u8, u8 | 0x428f08 | ds |
| 0x11 | u8, u8 | 0x42906e | sub_40D290 |
| 0x12 | u8, u8 | — | INVALID (default case) |
| 0x13 | u8, u8 | 0x42af5b | sub_422470, sub_43C3F0, _memset, sub_4224C0 |
| 0x14 | u8, u8 | — | INVALID (default case) |
| 0x15 | u8, u8 | 0x4291db |  |
| 0x16 | u8, u8 | 0x42e56f | Mes_GetLabelAddr, ds |
| 0x17 | u8, u8 | 0x429786 | sub_412D40 |
| 0x18 | u8, u8 | 0x42ba46 | Text_Display?, sub_40C610, sub_425F50, Save_WriteSlot, Backlog_InitGraphics, sub_406950, sub_402010, sub_4224C |
| 0x19 | u8, u8 | — | INVALID (default case) |
| 0x1A | u8, u8 | 0x4293b1 | Snd_?, sub_4043B0, sub_404260, sub_418F20, sub_413360, _memset, sub_41F440 |
| 0x1B | u8, u8 | 0x42985b |  |
| 0x1C | u8, u8 | 0x42ef92 | sub_41DEA0, Mes_GetLabelAddr |
| 0x1D | u8, u8 | 0x42eb46 | _memcpy, Mes_RunScriptByName, sub_4257D0, _memset, sub_41A6A0, sub_407810, sub_40E1E0, sub_407780 |
| 0x1E | u8, u8 | 0x428e8b | CG_CheckOrCapture, VM_AdvanceStream, SaveSelect_Update |
| 0x1F | u8, u8 | — | INVALID (default case) |
| 0x20 | u8, u8 | 0x42b112 | sub_4385A0 |
| 0x21 | u8, u8 | 0x4294df |  |
| 0x22 | u8, u8 | 0x429c7e | Snd_?, VM_AdvanceStream, Gfx_?, sub_435B70, sub_413360 |
| 0x23 | u8, u8 | — | INVALID (default case) |
| 0x24 | u8, u8 | 0x42b547 | sub_412F10, sub_4043B0, sub_404260, sub_418F20, sub_413360 |
| 0x25 | u8, u8 | 0x42e6ed | _rand, VM_Random, Text_Display?, sub_402320 |
| 0x26 | u8, u8 | 0x430613 | sub_44D440, sub_4106C0, ds |
| 0x27 | u8, u8 | — | INVALID (default case) |
| 0x28 | u8, u8 | 0x42ab12 | Sprite_BstPath, sub_4252C0, sub_421B30, sub_425D20, sub_41F490 |
| 0x29 | u8, u8 | — | INVALID (default case) |
| 0x2A | u8, u8 | — | INVALID (default case) |
| 0x2B | u8, u8 | — | INVALID (default case) |
| 0x2C | u8, u8 | — | INVALID (default case) |
| 0x2D | u8, u8 | — | INVALID (default case) |
| 0x2E | u8, u8 | — | INVALID (default case) |
| 0x2F | u8, u8 | — | INVALID (default case) |
| 0x30 | u8, u8 | — | INVALID (default case) |
| 0x31 | u8, u8 | — | INVALID (default case) |
| 0x32 | u8, u8 | 0x4301c2 | sub_412E90 |
| 0x33 | u8, u8 | 0x4301d2 | sub_412AF0, sub_412B00 |
| 0x34 | u8, u8 | 0x4301f6 | sub_412B10, sub_412B30, sub_412B40, sub_412B50, sub_412B60, sub_414710, sub_4146B0, sub_4146E0, sub_412B80 |
| 0x35 | u8, u8 | 0x43035d | sub_412B20 |
| 0x36 | u8, u8 | 0x43036d | sub_4227E0 |
| 0x37 | u8, u8 | 0x430379 |  |
| 0x38 | u8, u8 | 0x42f4aa | Gfx_QueueDraw, Gfx_DrawLayer, Gfx_SetLayerParam, sub_401040, sub_444340, sub_444450, Gfx_EffectSetup, sub_4479 |
| 0x39 | u8, u8 | 0x430db3 | sub_444B70 |
| 0x3A | u8, u8 | — | INVALID (default case) |
| 0x3B | u8, str | — | INVALID (default case) |
| 0x3C | u8, str | 0x42c11d | _atoi, sub_40CA50, _strstr, sub_4449F0, Gfx_QueueDraw, sub_41F870, sub_423AA0, Gfx_DrawLayer, _memset, Sprite_ |
| 0x3D | u8, str | 0x42930a | ds, Sprite_BstPath2 |
| 0x3E | u8, str | 0x42eb30 | sub_432D20 |
| 0x3F | u8, str | 0x42c6f0 | Sprite_BstPath2, sub_41E830, sub_447F40, Gfx_SetLayerParam, VM_VarRead, Gfx_QueueDraw, sub_444470, sub_4477E0, |
| 0x40 | u8, str | 0x42cb5f | __strnicmp, sub_44F850, Sprite_BstPath2, sub_41E830, sub_447F40, Gfx_SetLayerParam, VM_VarRead, sub_4477E0, su |
| 0x41 | u8, str | 0x42c557 | sub_41E830, sub_447F40, Sprite_BstPath |
| 0x42 | u8, str | — | INVALID (default case) |
| 0x43 | u8, str | 0x42e77f | sub_442EE0 |
| 0x44 | u8, str | 0x430090 | sub_40E290, sub_40E2C0 |
| 0x45 | u8, str | 0x430317 | sub_412F60, sub_412F50 |
| 0x46 | u8, str | 0x430121 | sub_40E430, sub_40E460 |
| 0x47 | u8, str | 0x430bd2 | sub_40E5E0 |
| 0x48 | str | — | INVALID (default case) |
| 0x49 | str | 0x42b158 | _memset, sub_425DB0, Sprite_BstPath2, sub_41E830, sub_447F40, Gfx_SetLayerParam |
| 0x4A | str | 0x42b2a3 | _memset, sub_425DB0, Sprite_BstPath2, sub_41E830, sub_447F40, Gfx_SetLayerParam, sub_444300, sub_40EAF0 |
| 0x4B | str | — | INVALID (default case) |
| 0x4C | str | 0x42e44e | sub_4224C0 |
| 0x4D | str | 0x42e50f | sub_4224C0 |
| 0x4E | str | 0x42e4af | sub_4224C0 |
| 0x4F | str | 0x42ecbf | _memset, sub_40EB10, Text_TagReplace?, sub_402A10, nullsub_1 |
| 0x50 | str | 0x42ed98 | sub_40EB10, sub_402A10, nullsub_1, Mes_RunScriptByName, sub_4094F0, sub_41FC90, _memset |
| 0x51 | str | 0x42ec98 |  |
| 0x52 | str | 0x42e61b | sub_425F50, sub_4224C0, sub_406D20, sub_453860 |
| 0x53 | str | — | INVALID (default case) |
| 0x54 | str | 0x42eb1c | sub_41FC90 |
| 0x55 | str | 0x4297a4 | sub_412D70 |
| 0x56 | str | 0x4297bc | sub_412D70 |
| 0x57 | str | — | INVALID (default case) |
| 0x58 | str | 0x429dec | sub_4359F0 |
| 0x59 | str | 0x42e9b3 | sub_41FC90, sub_418960 |
| 0x5A | str | 0x42ea7b | sub_418960, Gfx_?, sub_417970 |
| 0x5B | str | 0x42ea09 | sub_418960, Gfx_?, sub_417970 |
| 0x5C | str | 0x429881 | Mes_GetLabelAddr, sub_4076B0, sub_414BD0 |
| 0x5D | str | — | INVALID (default case) |
| 0x5E | str | 0x42efed | Cmd_ParseParams |
| 0x5F | str | 0x4298fc | Mes_GetLabelAddr |
| 0x60 | str | — | INVALID (default case) |
| 0x61 | str | 0x42e759 |  |
| 0x62 | str | — | INVALID (default case) |
| 0x63 | str | 0x428e21 |  |
| 0x64 | str | 0x42c687 |  |
| 0x65 | str | 0x430064 | sub_40E250, sub_40E100 |
| 0x66 | str | — | INVALID (default case) |
| 0x67 | str | 0x43089a | __strupr |
| 0x68 | str | 0x4308e7 | sub_44D260, sub_44D1C0 |
| 0x69 | TEXT (encstr +0x20) | — | INVALID (default case) |
| 0x6A | TEXT (encstr +0x20) | 0x42efed | Cmd_ParseParams |
| 0x6B | TEXT (encstr +0x20) | 0x42f043 | Cmd_WaitParams, Text_TagReplace?, sub_402B40, sub_43C4F0, sub_40E350, _sprintf, Cmd_ParseParams, _memset, _str |
| 0x6C | TEXT (encstr +0x20) | — | INVALID (default case) |
| 0x6D | TEXT (encstr +0x20) | 0x430882 | sub_4581D0 |
| 0x6E | u16 x4 | — | INVALID (default case) |
| 0x6F | u16 x4 | 0x42e012 | sub_40C9D0 |
| 0x70 | u16 x4 | 0x42e0d4 | sub_43C4F0, sub_40C9D0, sub_4224C0, _memset, sub_421A00 |
| 0x71 | u16 x4 | 0x428f28 | _memset, sub_4224C0, sub_422470, sub_40CA20 |
| 0x72 | u16 x4 | 0x4294fc | Mes_GetLabelAddr, sub_417BA0 |
| 0x73 | u16 x4 | 0x42958e | sub_412CE0, sub_4198A0, sub_419860, sub_414360, sub_418390 |
| 0x74 | u16 x4 | 0x429eac | sub_435BA0, sub_437AE0 |
| 0x75 | u16 x4 | 0x429f5f | sub_435A10 |
| 0x76 | u16 x4 | 0x4297d4 | Mes_GetLabelAddr, sub_4198F0 |
| 0x77 | u16 x4 | — | INVALID (default case) |
| 0x78 | u16 x4 | 0x429754 | sub_412E00 |
| 0x79 | u16 x4 | 0x42eaf0 |  |
| 0x7A | u16 x4 | 0x42b6cd | sub_412BA0, sub_417D20 |
| 0x7B | u16 x4 | 0x42b74a | sub_419360, sub_414740, sub_43D450, sub_441270, sub_43D4A0, Text_Display?, sub_43D440, sub_419640 |
| 0x7C | u16 x4 | 0x429720 | sub_412F20 |
| 0x7D | u16 x4 | 0x429d03 | sub_437750 |
| 0x7E | u16 x4 | 0x429d73 | sub_4379B0 |
| 0x7F | u16 x4 | 0x429da3 | sub_437A00 |
| 0x80 | u16 x4 | 0x429e04 | Mes_GetLabelAddr, sub_435AB0 |
| 0x81 | u16 x4 | 0x429d53 | sub_435B50 |
| 0x82 | u16 x4 | 0x42ad55 | sub_40EB30, sub_40EB60 |
| 0x83 | u16 x4 | 0x42addb | sub_40EB90 |
| 0x84 | u16 x4 | 0x42ae0b | Mes_GetLabelAddr, sub_438400, Text_Display? |
| 0x85 | u16 x4 | 0x42af1e | Text_Display? |
| 0x86 | u16 x4 | 0x42aff2 | Text_Display?, Mes_GetLabelAddr |
| 0x87 | u16 x4 | 0x42b84b | Mes_GetLabelAddr, sub_4076B0, sub_417F20 |
| 0x88 | u16 x4 | 0x42b82d | sub_412BC0 |
| 0x89 | u16 x4 | 0x42b8ec | sub_44A730, sub_401040, sub_444300, Gfx_EffectSetup |
| 0x8A | u16 x4 | — | INVALID (default case) |
| 0x8B | u16 x4 | 0x42d215 | sub_44A730, sub_401040, sub_444300, sub_4442C0, VM_VarRead, sub_447B00, Sprite_BstPath, Gfx_QueueDraw, sub_444 |
| 0x8C | u16 x4 | 0x42d6cf | sub_44A730, sub_401040, sub_444300, VM_VarRead, sub_447B00, Sprite_BstPath, Gfx_QueueDraw, sub_444470, sub_447 |
| 0x8D | u16 x4 | 0x42d1f3 | sub_401040 |
| 0x8E | u16 x4 | 0x42da46 | VM_VarRead, sub_447620, sub_447700, sub_4442E0, __ftol2_sse, sub_4442C0, Gfx_EffectSetup, sub_401040 |
| 0x8F | u16 x4 | 0x42dd29 | Gfx_DrawLayer, sub_4088F0, sub_447CE0, _sprintf, VM_VarRead, sub_447620, sub_447700, sub_401040, sub_44A730, G |
| 0x90 | u16 x4 | 0x42c0dc |  |
| 0x91 | u16 x4 | — | INVALID (default case) |
| 0x92 | u16 x4 | — | INVALID (default case) |
| 0x93 | u16 x4 | — | INVALID (default case) |
| 0x94 | u16 x4 | 0x42f23a | sub_44A730, sub_401040, sub_444300 |
| 0x95 | u16 x4 | 0x42f28e | Gfx_QueueDraw, Gfx_EffectSetup, sub_4478E0, sub_4479F0, sub_4477E0, Gfx_DrawLayer, sub_4088F0, sub_447CE0 |
| 0x96 | u16 x4 | 0x430042 | sub_4449B0 |
| 0x97 | u16 x4 | — | INVALID (default case) |
| 0x98 | u16 x4 | — | INVALID (default case) |
| 0x99 | u16 x4 | 0x42cd68 |  |
| 0x9A | u16 x4 | 0x42cd7a | sub_4227E0 |
| 0x9B | u16 x4 | 0x42cdab | sub_4227E0, _memset |
| 0x9C | u16 x4 | 0x42ce61 | sub_4227E0, _memset |
| 0x9D | u16 x4 | 0x42cf23 | sub_4449F0, sub_4227E0, _memset, sub_425530, VM_VarRead |
| 0x9E | u16 x4 | 0x4301aa |  |
| 0x9F | u16 x4 | 0x430389 | Gfx_QueueDraw, sub_444200, Gfx_DrawLayer, Gfx_SetLayerParam, sub_4444D0, sub_444340, Gfx_EffectSetup |
| 0xA0 | u16 x4 | 0x430487 | sub_444200, Gfx_DrawLayer, Gfx_SetLayerParam, sub_4444D0, sub_444340, Gfx_EffectSetup |
| 0xA1 | u16 x4 | 0x430570 | sub_401040 |
| 0xA2 | u16 x4 | 0x43059a |  |
| 0xA3 | u16 x4 | — | INVALID (default case) |
| 0xA4 | u16 x4 | 0x42aa36 | sub_40EF50 |
| 0xA5 | u16 x4 | 0x42a9d2 | sub_40EF90 |
| 0xA6 | u16 x4 | — | INVALID (default case) |
| 0xA7 | u16 x4 | 0x43066c | sub_445CC0 |
| 0xA8 | u16 x4 | 0x4306ba | sub_445CC0, sub_448560 |
| 0xA9 | u16 x4 | 0x42df4c | Text_Display?, VM_Random, sub_43BA50, sub_43B100, sub_43B140 |
| 0xAA | u16 x4 | 0x4306ee | sub_442BF0 |
| 0xAB | u16 x4 | 0x430710 | sub_458100, sub_4581F0, sub_458120, sub_458140, sub_4571C0, sub_457EB0, sub_457160, sub_457240, sub_4581B0, su |
| 0xAC | u16 x4 | 0x42b42b | sub_401040, sub_4088F0, Gfx_EffectSetup, sub_4478E0 |
| 0xAD | u16 x4 | 0x4308cf | sub_4359E0 |
| 0xAE | u16 x4 | 0x430919 | sub_444F50 |
| 0xAF | u16 x4 | 0x4300ca |  |
| 0xB0 | u16 x4 | 0x428dc3 |  |
| 0xB1 | u16 x4 | 0x430c17 | sub_444490, sub_444ED0 |
| 0xB2 | u16 x4 | 0x430c86 | VM_VarRead, sub_447E30, sub_444490, sub_444ED0 |
| 0xB3 | u16 x4 | 0x430d81 | sub_444490 |
| 0xB4 | u16 x4 | 0x42b28e |  |
| 0xB5 | u16 x4 | 0x430dff | VM_Random, sub_40E7E0, sub_414740, sub_440880, _memset, sub_407830, Text_TagReplace?, sub_402B40, sub_438A00,  |