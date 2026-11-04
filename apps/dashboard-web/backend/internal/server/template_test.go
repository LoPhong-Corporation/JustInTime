package server

import (
	"html/template"
	"io/fs"
	"testing"
)

func TestEmbeddedTemplatesParse(t *testing.T) {
	templateFS, err := fs.Sub(webFS, "web/templates")
	if err != nil {
		t.Fatal(err)
	}
	_, err = template.New("").Funcs(template.FuncMap{
		"json":        func(any) template.JS { return "" },
		"css":         func(string) template.CSS { return "" },
		"fmtDuration": func(int64) string { return "" },
		"fmtTime":     func(int64) string { return "" },
		"truncate":    func(int, string) string { return "" },
	}).ParseFS(templateFS, "*.html")
	if err != nil {
		t.Fatal(err)
	}
}
