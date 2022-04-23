//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"strings"

	"github.com/pkg/errors"
)

const (
	// reservedPropPrefix is prepended to property keys that are not
	// user-settable or viewable.
	reservedPropPrefix = "mgmt."
)

type (
	// SysPropSetter defines an interface to be implemented by
	// something that can set system properties.
	SysPropSetter interface {
		SetSystemProps(props map[string]string) error
	}
	// SysPropGetter defines an interface to be implemented by
	// something that can get system properties.
	SysPropGetter interface {
		GetSystemProps(keys []string) (map[string]string, error)
	}
)

// SetUserProperties updates the system properties with the supplied map.
// To delete a property, set the value to an empty string.
func SetUserProperties(db SysPropSetter, props map[string]string) error {
	for k := range props {
		if strings.HasPrefix(k, reservedPropPrefix) {
			return errors.Errorf("cannot set reserved property %q", k)
		}
	}

	return db.SetSystemProps(props)
}

// getProperties returns the system properties for the supplied keys.
func getProperties(db SysPropGetter, keys []string, toUser bool) (map[string]string, error) {
	props, err := db.GetSystemProps(keys)
	if err != nil {
		return nil, err
	}

	if toUser {
		for k := range props {
			if strings.HasPrefix(k, reservedPropPrefix) {
				delete(props, k)
			}
		}
	}

	return props, nil
}

// GetUserProperties returns the user-viewable system properties for the supplied keys.
func GetUserProperties(db SysPropGetter, keys []string) (map[string]string, error) {
	return getProperties(db, keys, true)
}

// SetMgmtProperty updates the mgmt property for the supplied key/value.
func SetMgmtProperty(db SysPropSetter, key, value string) error {
	key = reservedPropPrefix + key
	return db.SetSystemProps(map[string]string{key: value})
}

// GetMgmtProperty returns the mgmt property for the supplied key.
func GetMgmtProperty(db SysPropGetter, key string) (string, error) {
	key = reservedPropPrefix + key
	props, err := getProperties(db, []string{key}, false)
	if err != nil {
		return "", err
	}

	return props[key], nil
}

// DelMgmtProperty deletes the mgmt property for the supplied key.
func DelMgmtProperty(db SysPropSetter, key string) error {
	return SetMgmtProperty(db, key, "")
}
