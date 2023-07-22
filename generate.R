# Generate code to paste into source files

library(bitops)

paste0(paste0("0x", as.hexmode((1L %<<% c(0:15)) - 1)), collapse = ",")
paste0(paste0("0x", as.hexmode((1L %<<% c(0:63)) - 1)), collapse = ",")
