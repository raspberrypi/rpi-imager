import os
import json
import pytest
from jsonschema import validate
from jsonschema.exceptions import ValidationError


def test_os_list_json_against_schema(oslisttuple, schema):
    oslisturl = oslisttuple[0]
    oslistdata = oslisttuple[1]
    errorMsg = ""

    j = json.loads(oslistdata)

    try:
        validate(instance=j, schema=schema)
    except ValidationError as err:
        errorMsg = err.message

    if errorMsg != "":
        pytest.fail(oslisturl+" failed schema validation: "+errorMsg, False)


@pytest.fixture
def schema():
    f = open(os.path.dirname(__file__)+"/../doc/json-schema/os-list-schema.json","r")
    data = f.read()
    return json.loads(data)
