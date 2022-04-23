//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

type testPropDb struct {
	props map[string]string
}

func (db *testPropDb) SetSystemProps(props map[string]string) error {
	for k := range props {
		if props[k] == "" {
			delete(db.props, k)
			continue
		}
		db.props[k] = props[k]
	}
	return nil
}

func (db *testPropDb) GetSystemProps(keys []string) (map[string]string, error) {
	out := make(map[string]string)
	if len(keys) == 0 {
		for k, v := range db.props {
			out[k] = v
		}
		return out, nil
	}

	for _, k := range keys {
		if v, ok := db.props[k]; ok {
			out[k] = v
			continue
		}
		return nil, ErrSystemPropNotFound(k)
	}
	return out, nil
}

func newPropDb(props map[string]string) *testPropDb {
	if props == nil {
		props = make(map[string]string)
	}

	return &testPropDb{
		props: props,
	}
}

func TestSystem_SetUserProperties(t *testing.T) {
	for name, tc := range map[string]struct {
		userProps map[string]string
		expErr    error
	}{
		"reserved prop": {
			userProps: map[string]string{
				reservedPropPrefix + "foo": "bar",
			},
			expErr: errors.New("reserved property"),
		},
		"success": {
			userProps: map[string]string{
				"foo": "bar",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := SetUserProperties(newPropDb(nil), tc.userProps)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestSystem_GetUserProperties(t *testing.T) {
	reservedPropKey := reservedPropPrefix + "system-stuff"
	propDb := newPropDb(map[string]string{
		"foo":           "bar",
		"baz":           "qux",
		reservedPropKey: "buzz off",
	})
	for name, tc := range map[string]struct {
		propKeys []string
		expProps map[string]string
		expErr   error
	}{
		"query for reserved prop is filtered": {
			propKeys: []string{reservedPropKey},
			expProps: map[string]string{},
		},
		"query for all filters reserved": {
			expProps: map[string]string{
				"foo": "bar",
				"baz": "qux",
			},
		},
		"query for single prop": {
			propKeys: []string{"foo"},
			expProps: map[string]string{
				"foo": "bar",
			},
		},
		"unknown prop": {
			propKeys: []string{"bananas"},
			expErr:   ErrSystemPropNotFound("bananas"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotProps, gotErr := GetUserProperties(propDb, tc.propKeys)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expProps, gotProps); diff != "" {
				t.Fatalf("unexpected properties (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSystem_MgmtProperties(t *testing.T) {
	propDb := newPropDb(nil)

	if err := SetMgmtProperty(propDb, "foo", "bar"); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(map[string]string{reservedPropPrefix + "foo": "bar"}, propDb.props); diff != "" {
		t.Fatalf("unexpected properties (-want +got):\n%s", diff)
	}

	gotVal, err := GetMgmtProperty(propDb, "foo")
	if err != nil {
		t.Fatal(err)
	}
	if gotVal != "bar" {
		t.Fatalf("unexpected value: want %q, got %q", "bar", gotVal)
	}

	if err := DelMgmtProperty(propDb, "foo"); err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(map[string]string{}, propDb.props); diff != "" {
		t.Fatalf("unexpected properties (-want +got):\n%s", diff)
	}
}
