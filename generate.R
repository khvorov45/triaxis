# Generate code to paste into source files

paste0(paste0("0x", as.hexmode((2 ^ c(0:15)) - 1)), collapse = ",")

paste0(
    c(
        paste0(paste0("0x", as.hexmode((2 ^ c(0:31)) - 1), "ULL"), collapse = ","),
        paste0(paste0("0x", as.hexmode((2 ^ c(0:31)) - 1), "ffffffffULL"), collapse = ",")
    ),
    collapse = ","
)
