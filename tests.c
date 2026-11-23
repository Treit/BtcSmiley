#include "BtcSmiley.c"
#include <assert.h>

int main(int argc, char **argv)
{
    Quote quote = ParseQuote("{\"open\":\"100\",\"last\":\"90\"}");
    const char *invalid[] = {
        "{}", "{\"message\":\"unavailable\"}",
        "{\"open\":\"0\",\"last\":\"90\"}",
        "{\"open\":\"100\",\"last\":\"-1\"}",
        "{\"open\":\"nan\",\"last\":\"90\"}",
        "{\"open\":\"100\",\"last\":\"inf\"}",
        "{\"open\":\"1e309\",\"last\":\"90\"}",
        "{\"open\":\"100\",\"last\":\"90oops\"}",
        "{\"open\":\"100\",\"last\":90}",
        "{\"open\":\"100\",\"last\":\"90}",
        "{\"open\":\"1e-300\",\"last\":\"1e300\"}"
    };
    size_t i;
    int mood;
    assert(quote.valid && quote.price == 90);
    assert(fabs(quote.change + 10) < 0.000001);
    assert(Mood(quote.change, FALSE) == 2);
    assert(Mood(quote.change, TRUE) == 0);
    quote = ParseQuote("{ \"last\" : \"110\", \"open\" : \"100\" }");
    assert(quote.valid && fabs(quote.change - 10) < 0.000001);
    assert(Mood(quote.change, FALSE) == 0);
    assert(Mood(quote.change, TRUE) == 2);
    assert(Mood(0, FALSE) == 1 && Mood(0, TRUE) == 1);
    assert(Mood(0.1, FALSE) == 1 && Mood(-0.1, TRUE) == 1);
    assert(Mood(0.10001, FALSE) == 0 && Mood(-0.10001, TRUE) == 0);
    assert(Mood(0.05, TRUE) == 1 && Mood(-0.05, FALSE) == 1);

    for (i = 0; i < ARRAYSIZE(invalid); ++i) {
        assert(!ParseQuote(invalid[i]).valid);
    }

    for (mood = 0; mood < 3; ++mood) {
        HICON icon = DrawFace(mood);
        ICONINFO info = {0};
        assert(icon && GetIconInfo(icon, &info));
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
        faces[mood] = icon;
    }

    current = quote;
    UpdateTray(NIM_MODIFY);
    assert(tray.hIcon == faces[0]);
    assert(wcsstr(tray.szTip, L"BTC $110.00 | 24h +10.00% | Bear"));
    bull = TRUE;
    UpdateTray(NIM_MODIFY);
    assert(tray.hIcon == faces[2]);
    assert(wcsstr(tray.szTip, L"Bull"));
    stale = TRUE;
    UpdateTray(NIM_MODIFY);
    assert(tray.hIcon == faces[2]);
    assert(wcsstr(tray.szTip, L"$110.00"));
    assert(wcsstr(tray.szTip, L"stale: update failed"));
    current.valid = FALSE;
    UpdateTray(NIM_MODIFY);
    assert(tray.hIcon == faces[1]);
    assert(wcsstr(tray.szTip, L"Price unavailable"));

    for (mood = 0; mood < 3; ++mood) {
        DestroyIcon(faces[mood]);
    }

    if (argc > 1 && strcmp(argv[1], "--live") == 0) {
        quote = FetchQuote();
        assert(quote.valid);
        printf("Live BTC/USD: $%.2f, 24h %+.2f%%\n", quote.price, quote.change);
    }

    puts("All tests passed.");
    return 0;
}
