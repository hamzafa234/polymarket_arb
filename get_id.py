import requests

GAMMA_API = "https://gamma-api.polymarket.com/markets"
CLOB_API = "https://clob.polymarket.com/markets"


def get_token_ids_by_slug(slug: str):
    """
    Look up a market by its Polymarket slug (the part of the URL after
    /event/... or /market/...) and return its outcome token IDs.
    """
    resp = requests.get(GAMMA_API, params={"slug": slug})
    resp.raise_for_status()
    data = resp.json()

    if not data:
        raise ValueError(f"No market found for slug: {slug}")

    market = data[0]
    condition_id = market.get("conditionId")

    # clobTokenIds comes back as a JSON-encoded string, e.g. '["123...", "456..."]'
    import json
    token_ids = json.loads(market.get("clobTokenIds", "[]"))
    outcomes = json.loads(market.get("outcomes", "[]"))

    return {
        "condition_id": condition_id,
        "tokens": dict(zip(outcomes, token_ids)),
    }


def get_market_by_condition_id(condition_id: str):
    """
    Query the CLOB API directly for full market details (order book info,
    tick size, etc.) using the condition ID.
    """
    resp = requests.get(f"{CLOB_API}/{condition_id}")
    resp.raise_for_status()
    return resp.json()


if __name__ == "__main__":
    # Example: replace with a real market slug from the Polymarket URL
    slug = "will spy hit 760 in july 2026"

    result = get_token_ids_by_slug(slug)
    print("Condition ID:", result["condition_id"])
    print("Tokens by outcome:")
    for outcome, token_id in result["tokens"].items():
        print(f"  {outcome}: {token_id}")
