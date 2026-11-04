package server

import "testing"

func TestCSVSafeCell(t *testing.T) {
	tests := []struct {
		input string
		want  string
	}{
		{input: "plain.exe", want: "plain.exe"},
		{input: "=1+1", want: "'=1+1"},
		{input: "  @SUM(A1:A2)", want: "'  @SUM(A1:A2)"},
		{input: "-1", want: "'-1"},
	}
	for _, test := range tests {
		if got := csvSafeCell(test.input); got != test.want {
			t.Errorf("csvSafeCell(%q) = %q; want %q", test.input, got, test.want)
		}
	}
}
