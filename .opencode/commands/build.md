---
description: Compila o firmware ESP-IDF
agent: build
---

Carregue `esp-idf-validation`.

Argumentos:

`$ARGUMENTS`

Se houver `clean` ou `reconfigure`, execute:

```powershell
.\scripts\build.ps1 -Reconfigure
```

Caso contrario:

```powershell
.\scripts\build.ps1
```

Nao faca flash automaticamente.