local grp = vim.api.nvim_create_augroup("ProjectCStyle", { clear = true })

vim.api.nvim_create_autocmd({ "FileType" }, {
	group = grp,
	-- pattern = { "c", "cpp", "cmake", "sh" },
	pattern = { "cmake", "sh" },
	callback = function()
		vim.b.autoformat = false
	end,
})

vim.api.nvim_create_autocmd({ "FileType" }, {
	group = grp,
	-- pattern = { "c", "cpp", "cmake", "sh" },
	pattern = { "c", "cpp" },
	callback = function()
		vim.b.autoformat = true
	end,
})

-- Set tab settings specifically for C and C++ files
vim.api.nvim_create_autocmd("FileType", {
	group = grp,
	pattern = { "c", "cpp", "yacc", "lex" },
	callback = function()
		vim.bo.cindent = true
		vim.bo.indentexpr = ""
		vim.bo.cinoptions = "j1,f0,^-2,{2,>4,:4,n-2,(0,t0"
		vim.bo.shiftwidth = 2
		vim.bo.tabstop = 8
		vim.bo.expandtab = false
	end,
})

-- Optional: shell files separate (C-style cindent is wrong for sh)
vim.api.nvim_create_autocmd("FileType", {
	group = grp,
	pattern = { "sh" },
	callback = function()
		-- no cindent for shell files
		vim.bo.expandtab = true
		vim.bo.shiftwidth = 2
		vim.bo.softtabstop = 2
		vim.bo.tabstop = 2
	end,
})

vim.filetype.add({
	extension = { i = "c" },
})

local conform = require("conform")

-- install indent <= 2.2.11
-- for example, nix indent 2.2.10 is
-- nix profile install nixpkgs/22f65339f3773f5b691f55b8b3a139e5582ae85b#indent
conform.formatters.cubrid_c = {
	command = "cubrid-format-codestyle.sh",
	args = { "$FILENAME" },
	stdin = false,
}

conform.formatters_by_ft.c = { "cubrid_c" }
conform.formatters_by_ft.cpp = { "cubrid_c" }
