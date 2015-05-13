#include "stdafx.h"

void CDropbox::CommandHelp(void *arg)
{
	CommandParam *param = (CommandParam*)arg;

	CMStringA help(Translate("Dropbox supports the following commands:")); help += "\n";
	help += "\"/content [dir]\" - "; help += Translate("shows all files in folder \"dir\" (\"dir\" can be omitted for root folder)"); help += "\n";
	help += "\"/share <path>\" - "; help += Translate("returns download link for file or folder with specified path (\"path\" is relative from root folder)"); help += "\n";
	help += "\"/delete <path>\" - "; help += Translate("deletes file or folder with specified path (\"path\" is relative from root folder)");

	ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
	CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)help.GetBuffer());
}

void CDropbox::CommandContent(void *arg)
{
	CommandParam *param = (CommandParam*)arg;

	char *path = (char*)param->data;
	if (path)
	{
		ptrA token(db_get_sa(NULL, MODULE, "TokenSecret"));
		ptrA encodedPath(mir_utf8encode(path));
		GetMetadataRequest request(token, encodedPath);
		mir_ptr<NETLIBHTTPREQUEST> response(request.Send(param->instance->hNetlibConnection));

		if (response && response->resultCode == HTTP_STATUS_OK)
		{
			CMStringA message;

			JSONROOT root(response->pData);
			if (root)
			{
				JSONNODE *node = json_get(root, "is_dir");
				bool isDir = json_as_bool(node) > 0;
				if (!isDir)
					message.AppendFormat("\"%s\" %s", path, Translate("is file"));
				else
				{
					JSONNODE *content = json_as_array(json_get(root, "contents"));
					for (int i = 0;; i++) {
						JSONNODE *item = json_at(content, i);
						if (item == NULL) {
							if (i == 0)
								message.AppendFormat("\"%s\" %s", path, Translate("is empty"));
							break;
						}

						ptrA subName(mir_u2a(json_as_string(json_get(item, "path"))));
						message.AppendFormat("%s\n", (subName[0] == '/') ? &subName[1] : subName);
					}
				}

				ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
				CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)message.GetBuffer());

				return;
			}
		}
	}
	else
	{
		CMStringA error(FORMAT, Translate("\"%s\" command has invalid parameter.\nUse \"/help\" for more info."), "/content");
		ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
		CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)error.GetBuffer());

		return;
	}

	ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, param->hProcess, 0);
}

void CDropbox::CommandShare(void *arg)
{
	CommandParam *param = (CommandParam*)arg;

	char *path = (char*)param->data;
	if (path)
	{
		ptrA token(db_get_sa(NULL, MODULE, "TokenSecret"));
		ptrA encodedPath(mir_utf8encode(path));
		bool useShortUrl = db_get_b(NULL, MODULE, "UseSortLinks", 1) > 0;
		ShareRequest request(token, encodedPath, useShortUrl);
		mir_ptr<NETLIBHTTPREQUEST> response(request.Send(param->instance->hNetlibConnection));

		if (response && response->resultCode == HTTP_STATUS_OK)
		{
			CMStringA link;

			JSONROOT root(response->pData);
			if (root)
			{
				JSONNODE *node = json_get(root, "url");
				link = mir_u2a(json_as_string(node));
				ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
				CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)link.GetBuffer());

				return;
			}
		}
	}
	else
	{
		CMStringA error(FORMAT, Translate("\"%s\" command has invalid parameter.\nUse \"/help\" for more info."), "/share");
		ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
		CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)error.GetBuffer());

		return;
	}

	ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, param->hProcess, 0);
}

void CDropbox::CommandDelete(void *arg)
{
	CommandParam *param = (CommandParam*)arg;

	char *path = (char*)param->data;
	if (path)
	{
		ptrA token(db_get_sa(NULL, MODULE, "TokenSecret"));
		ptrA encodedPath(mir_utf8encode(path));
		DeleteRequest request(token, encodedPath);
		mir_ptr<NETLIBHTTPREQUEST> response(request.Send(param->instance->hNetlibConnection));

		if (response && response->resultCode == HTTP_STATUS_OK)
		{
			JSONROOT root(response->pData);
			if (root)
			{
				JSONNODE *node = json_get(root, "is_deleted");
				bool isDeleted = json_as_bool(node) > 0;
				CMStringA message(FORMAT, "%s %s", path, !isDeleted ? Translate("is not deleted") : Translate("is deleted"));
				ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
				CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)message.GetBuffer());
				return;
			}
		}
	}
	else
	{
		CMStringA error(FORMAT, Translate("\"%s\" command has invalid parameter.\nUse \"/help\" for more info."), "/delete");
		ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, param->hProcess, 0);
		CallContactService(param->instance->GetDefaultContact(), PSR_MESSAGE, 0, (LPARAM)error.GetBuffer());
	}

	ProtoBroadcastAck(MODULE, param->hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, param->hProcess, 0);
}