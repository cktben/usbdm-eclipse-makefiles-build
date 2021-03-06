/*
 * AppSettings.h
 *
 *  Created on: 01/03/2010
 *      Author: podonoghue
 */
#ifndef APPSETTINGS_HPP_
#define APPSETTINGS_HPP_

#include <map>
#include "USBDM_API.h"

using namespace std;

class AppSettings {

private:
   enum ValueType {intType, stringType};
   //! Class to encapsulate the attribute & type
   class Value {
      private:
         ValueType type;
         int       intValue;
         string    stringValue;
      public:
         //! Create a integer attribute
         //!
         //! @param value - value to create
         Value(int value) {
            intValue = value;
            type     = intType;
         }
         //! Create a string attribute
         //!
         //! @param value - value to create
         Value(const string &value) :
            intValue(0)
         {
            stringValue = value;
            type        = stringType;
         }
         //! Create a string attribute
         //!
         //! @param value - value to create
         Value(const char *value) :
            intValue(0)
         {
            if (value == NULL)
               value = " ";
            stringValue = string(value);
            type        = stringType;
         }
         //! Obtain the integer value
         //!
         int getIntValue() const {
            return intValue;
         }
         //! Obtain the string value
         //!
         const string &getStringValue() const {
            return stringValue;
         }
         //! Obtain the type of the attribute
         //!
         ValueType getType() const {
            return type;
         }
   };

   map<string,Value *>  mymap;    //!< Container for key/attribute pairs
   string               fileName; //!< Path to load/save settings from

   void loadFromFile(FILE *fp);
   void writeToFile(FILE *fp, const string &comment) const;

   static string getSettingsFilename(const string &rootFilename, TargetType_t targetType);

public:
   //! Add a integer attribute
   //!
   //! @param key - key to use to save/retrieve the attribute
   //! @param value - value to save
   void addValue(string const &key, int value) {
      mymap[key] = new Value(value);
   }
   //! Add a string attribute
   //!
   //! @param key   - key to use to save the attribute
   //! @param value - value to save
   void addValue(string const &key, string const &value) {
      mymap[key] = new Value(value);
   }
   //! Add a string attribute
   //!
   //! @param key   - key to use to save the attribute
   //! @param value - value to save
   void addValue(string const &key, const char *value) {
      mymap[key] = new Value(value);
   }
   //! Retrieve an integer attribute
   //!
   //! @param key      - key to use to retrieve the attribute
   //! @param defValue - default value to use if not found
   int getValue(string const &key, int defValue) const {
      map<string,Value *>::const_iterator it;
      it = mymap.find(key);
      if (it != mymap.end())
         return it->second->getIntValue();
      else
         return defValue;
   }
   //! Retrieve an string attribute
   //!
   //! @param key      - key to use to retrieve the attribute
   //! @param defValue - default value to use if not found
   string getValue(string const &key, string const &defValue) const {
      map<string,Value *>::const_iterator it;
      it = mymap.find(key);
      if (it != mymap.end())
         return it->second->getStringValue();
      else
         return defValue;
   }

//   bool loadFromFile();
//   bool writeToFile(string const &comment = string()) const;

   bool loadFromAppDirFile();
   bool writeToAppDirFile(const string &comment = string()) const;

   void printToLog() const;

   AppSettings(string baseFilename, TargetType_t targetType) :
      fileName(getSettingsFilename(baseFilename, targetType)) {

   }

};

#endif /* APPSETTINGS_HPP_ */
